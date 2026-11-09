#include "detent_local/disk_store.hh"
#include "dense_legacy_fixture.hh"
#include "managed_recovery.hh"
#include "generation_mapping.hh"
#include "lazy_page_access.hh"
#include "paged_disk_fixture.hh"
#include "paged_snapshot_plan.hh"
#include <completion/native_completion.h>
#include <cstring>
#include <fcntl.h>
#include <source_location>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>

namespace c = kronuz::io::completion;
extern char **environ;
std::string executable;
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("dense refetch invariant at " + std::to_string(where.line()));
}
void tick(const std::shared_ptr<l::DiskStore> &store) {
	if (auto operation = store->operation()) {
		const auto request = operation->request();
		operation->submitted();
		check(operation->complete(kronuz::journal::detail::execute_primitive(operation->io(), request)));
	}
	store->advance();
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	store->advance();
	for (unsigned turn = 0; store->operation() || store->needs_work(); ++turn) {
		check(turn < 1000000);
		tick(store);
	}
}
l::DiskUsage census(const std::filesystem::path &directory) {
	l::DiskUsage usage;
	for (const auto &entry : std::filesystem::directory_iterator(directory)) {
		++usage.entries;
		usage.bytes += entry.file_size();
		if (entry.path().extension() == ".lease")
			++usage.generations;
	}
	return usage;
}
struct Runtime {
	std::shared_ptr<d::StorageLedger> heap = std::make_shared<d::StorageLedger>(64ull << 20);
	std::shared_ptr<d::StorageLedger> mappings = std::make_shared<d::StorageLedger>(16ull << 20);
	std::shared_ptr<d::StorageLedger> controls = std::make_shared<d::StorageLedger>(1ull << 20);
	d::RetirementRuntime runtime{controls, 1};
	d::RetirementDomain domain = runtime.domain(heap, 512);
	l::DiskStoreOptions options;
	Runtime() {
		options.limits = {64ull << 20, 1000, 20};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = mappings;
	}
	std::shared_ptr<l::DiskStore> open(const std::filesystem::path &directory) {
		return l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
	}
	~Runtime() {
		domain.close().wait();
		check(mappings->used() == 0);
	}
};
l::Descriptor full_copy(Runtime &runtime, const std::filesystem::path &directory,
						const std::shared_ptr<l::DiskStore> &store, char generation = 'r', bool interrupt = false) {
	const auto context = l::detail::ManagedRecovery::capture(*store);
	l::GenerationId id;
	id.fill(generation);
	d::Limits limits;
	limits.storage_account = runtime.heap;
	d::SnapshotDecoder decoder(cursor(), {"app"}, limits);
	const auto wire = fixture();
	auto input = std::span<const char>(wire);
	while (!input.empty()) {
		d::WorkBudget budget;
		decoder.feed(input, budget);
	}
	auto source = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
	l::detail::PagedSnapshotPlanBuilder builder(source, {}, context.directory, context.storage, id,
											  runtime.domain.factory(), runtime.heap, runtime.options.recovery_limits);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto plan = builder.take();
	const auto wanted = plan.descriptor();
	store->begin(std::move(plan));
	if (interrupt) {
		for (unsigned turn = 0; store->operation(); ++turn) {
			check(turn < 1000000);
			auto operation = store->operation();
			auto request = operation->request();
			if (request.kind == c::Kind::Write && request.bytes.size() == l::paged::root_bytes &&
				request.bytes.substr(0, 8) == "DTROOT03") {
				request.bytes = request.bytes.substr(0, 13);
				operation->submitted();
				const auto result = kronuz::journal::detail::execute_primitive(operation->io(), request);
				check(!result.error && result.count == 13);
				// Model process death after a short root write, before completion or selected replacement.
				::_exit(0);
			}
			tick(store);
		}
		throw std::runtime_error("interrupted copy never wrote a root");
	}
	drive(store);
	check(store->status() == l::StoreStatus::Ready &&
		  store->publication_status() == l::DiskPublicationStatus::Published);
	{
		auto recovered = l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store));
		check(recovered.descriptor() == wanted && recovered.view().size() == 2 &&
			  recovered.view().get("app", "count").value() == INT64_MIN &&
			  recovered.view().eval("app", "flag", "host", "beta").value() ==
				  (detent::stable_bucket("app:flag", "host") < 17));
		store->confirm_recovered(recovered);
	}
	store->request_collection();
	drive(store);
	check(!store->collection_error() && store->stats().used == census(directory));
	return wanted;
}
void close(const std::shared_ptr<l::DiskStore> &store) {
	store->close();
	drive(store);
	check(store->drained());
}
void crash_at(const std::filesystem::path &directory, unsigned cut) {
	Runtime runtime;
	auto store = runtime.open(directory);
	for (unsigned turn = 0; store->status() == l::StoreStatus::Starting; ++turn) {
		check(turn < 1000000);
		auto operation = store->operation();
		check(bool(operation));
		const auto request = operation->request();
		if (request.kind == c::Kind::Remove && request.source == "selected") {
			check(store->selection().knowledge == l::SelectionKnowledge::Uncertain);
			if (cut == 0)
				::_exit(0);
			operation->submitted();
			check(operation->complete(kronuz::journal::detail::execute_primitive(operation->io(), request)));
			check(store->status() == l::StoreStatus::Starting &&
				  store->selection().knowledge == l::SelectionKnowledge::Uncertain &&
				  operation->request().kind == c::Kind::DirectorySync);
			if (cut == 1)
				::_exit(0);
			continue;
		}
		tick(store);
	}
	check(store->status() == l::StoreStatus::Ready &&
		  store->selection().knowledge == l::SelectionKnowledge::Absent);
	if (cut == 2)
		::_exit(0);
	drive(store);
	full_copy(runtime, directory, store, 'n', true);
}
void child_crash(const std::filesystem::path &directory, unsigned cut) {
	const auto path = directory.string();
	auto argument = std::to_string(cut);
	char *arguments[]{executable.data(), const_cast<char *>("--crash"),
		const_cast<char *>(path.c_str()), argument.data(), nullptr};
	pid_t child = -1;
	check(::posix_spawn(&child, executable.c_str(), nullptr, nullptr, arguments, environ) == 0);
	int status = 0;
	check(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
void restart_copy(const std::filesystem::path &directory, unsigned cut) {
	dense_legacy::install(directory);
	child_crash(directory, cut);
	check(std::filesystem::exists(directory / "selected") == (cut == 0));
	l::Descriptor wanted;
	{
		Runtime runtime;
		auto store = runtime.open(directory);
		drive(store);
		if (store->error())
			std::rethrow_exception(store->error());
		check(store->status() == l::StoreStatus::Ready &&
			  store->selection().knowledge == l::SelectionKnowledge::Absent &&
			  !std::filesystem::exists(directory / "selected"));
		check(store->stats().used == census(directory));
		wanted = full_copy(runtime, directory, store);
		close(store);
	}
	{
		Runtime runtime;
		auto store = runtime.open(directory);
		drive(store);
		check(store->status() == l::StoreStatus::Ready && store->selection().descriptor == wanted);
		close(store);
	}
}
std::vector<char> read(const std::filesystem::path &path) {
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void reseal(std::vector<char> &bytes) {
	check(bytes.size() >= 32);
	const auto digest = l::paged::metadata_commitment(std::span(bytes).first(bytes.size() - 32));
	std::copy(digest.begin(), digest.end(), bytes.end() - 32);
}
void damaged(const std::filesystem::path &directory, unsigned mutation) {
	dense_legacy::install(directory);
	l::GenerationId id;
	id.fill('g');
	const auto image_path = directory / (l::generation_name(id) + ".image");
	const auto lease_path = directory / (l::generation_name(id) + ".lease");
	auto bytes = read(image_path);
	auto lease = read(lease_path);
	if (mutation == 0) {
		bytes[20] ^= 1; // A framing-correct old root with an incorrect selected checksum is damage.
		write(image_path, bytes);
	} else if (mutation == 1) {
		l::StorageIdentity wrong;
		wrong.fill('w');
		write(directory / "storage.meta", l::encode_storage_identity(wrong));
	} else if (mutation == 2) {
		lease[32] ^= 1;
		reseal(lease);
		write(lease_path, lease);
	} else if (mutation == 3) {
		// Version-3 ownership never turns old-looking damage into deletion authority.
		l::write_integer(lease, 56, 3, 4);
		reseal(lease);
		write(lease_path, lease);
	} else if (mutation == 4) {
		bytes[7] = '9';
		const auto digest = l::paged::metadata_commitment(bytes);
		std::copy(digest.begin(), digest.end(), lease.begin() + 64);
		reseal(lease);
		write(lease_path, lease);
		write(image_path, bytes);
		auto descriptor = l::decode_descriptor(read(directory / "selected"));
		descriptor.checksum = digest;
		write(directory / "selected", l::encode_descriptor(descriptor));
	} else if (mutation == 5) {
		write(lease_path, {}); // An unowned old image has no cleanup authority.
	} else {
		const int lock = ::open((directory / "owner.lock").c_str(), O_RDONLY | O_CLOEXEC);
		check(lock >= 0 && ::flock(lock, LOCK_EX | LOCK_NB) == 0);
		Runtime runtime;
		bool rejected = false;
		try {
			runtime.open(directory);
		} catch (const std::exception &) {
			rejected = true;
		}
		check(rejected && std::filesystem::exists(directory / "selected"));
		::close(lock);
		return;
	}
	const auto selected = read(directory / "selected");
	Runtime runtime;
	auto store = runtime.open(directory);
	drive(store);
	check(store->status() == l::StoreStatus::Fenced && store->error() &&
		  read(directory / "selected") == selected && std::filesystem::exists(image_path));
	close(store);
}
void leased(const std::filesystem::path &directory) {
	dense_legacy::install(directory);
	write(directory / "unowned.pack", std::string_view("accounted foreign data"));
	l::GenerationId id;
	id.fill('g');
	const auto name = l::generation_name(id);
	const int lease = ::open((directory / (name + ".lease")).c_str(), O_RDONLY | O_CLOEXEC);
	check(lease >= 0 && ::flock(lease, LOCK_SH | LOCK_NB) == 0);
	Runtime runtime;
	auto store = runtime.open(directory);
	drive(store);
	check(store->status() == l::StoreStatus::Ready &&
		  store->selection().knowledge == l::SelectionKnowledge::Absent &&
		  store->stats().used == census(directory) && store->stats().used.generations == 1 &&
		  std::filesystem::exists(directory / (name + ".image")));
	// The held old root prevents the pack sweep; all bytes remain charged.
	check(store->collection_error() && !store->error());
	::close(lease);
	store->request_collection();
	drive(store);
	check(!store->collection_error() && store->stats().used.generations == 0 &&
		  store->stats().used == census(directory) &&
		  !std::filesystem::exists(directory / (name + ".lease")) &&
		  std::filesystem::exists(directory / "unowned.pack"));
	for (const auto &entry : std::filesystem::directory_iterator(directory))
		check(entry.path().extension() != ".pack" || entry.path().filename() == "unowned.pack");
	full_copy(runtime, directory, store);
	close(store);
}
void embedded(const std::filesystem::path &directory) {
	dense_legacy::install(directory);
	std::vector<char> page;
	for (const auto &entry : std::filesystem::directory_iterator(directory)) {
		if (entry.path().extension() == ".pack") {
			page = read(entry.path());
			page.resize(l::read_integer(page, 16, 4));
			break;
		}
	}
	check(page.size() >= 80);
	bool refused = false;
	try {
		l::paged::decode_page_header(page, l::paged::Limits{});
	} catch (const l::paged::UnsupportedFormat &) {
		refused = true;
	}
	check(refused);
	Runtime runtime;
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{}, segment{};
	storage.fill('s');
	generation.fill('v');
	segment.fill('p');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	auto root = image.root();
	root.records = root.pages = root.segments = 1;
	root.namespaces = 0;
	root.bytes = page.size();
	root.tree = {segment, 0, static_cast<std::uint32_t>(page.size()), 1, 1, {}};
	root.tree.digest.fill('d');
	const auto bytes = l::paged::encode_root(root, l::paged::Limits{});
	auto descriptor = image.descriptor();
	descriptor.checksum = l::paged::metadata_commitment(bytes);
	const auto name = l::generation_name(generation);
	write(directory / (name + ".image"), bytes);
	write(directory / (name + ".lease"), l::encode_generation_ownership(
		{storage, generation, bytes.size(), descriptor.checksum, 3}));
	const l::detail::SegmentNames names(segment);
	write(directory / names.payload_name(), page);
	write(directory / names.owner_name(), l::encode_segment_ownership(
		{storage, segment, page.size(), l::paged::metadata_commitment(page)}));
	write(directory / "selected", l::encode_descriptor(descriptor));
	refused = false;
	try {
		l::recover_generation(directory, runtime.options.recovery_limits);
	} catch (const l::paged::UnsupportedFormat &) {
		refused = true;
	}
	check(refused && l::read_generation_selection(directory).descriptor == descriptor);
	{
		auto held = std::make_shared<l::detail::RecoveryFile>(
			::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
		check(held->fd >= 0);
		auto pages = runtime.domain.make<l::detail::LazyPageAccess>(
			held, held->fd, ::getuid(), storage, root, runtime.options.recovery_limits,
			runtime.domain.factory(), runtime.heap, runtime.mappings);
		refused = false;
		try {
			pages->resolve(root.tree);
		} catch (const l::detail::ReadSourceFailure &) {
			refused = true;
		}
		check(refused && !pages->available() && pages->page_verifications() == 0);
	}
	// V3 damage is fenced at managed startup; it cannot authorize selected removal.
	auto store = runtime.open(directory);
	drive(store);
	check(store->status() == l::StoreStatus::Fenced &&
		  l::read_generation_selection(directory).descriptor == descriptor);
	close(store);
}
int main(int argc, char **argv) {
	if (argc == 4 && std::string_view(argv[1]) == "--crash") {
		try {
			crash_at(argv[2], static_cast<unsigned>(std::stoul(argv[3])));
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
		}
		return 90;
	}
	executable = std::filesystem::absolute(argv[0]).string();
	const auto directory = std::filesystem::current_path() / ".scratch" /
		("dense-refetch-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{directory};
	for (unsigned cut = 0; cut != 4; ++cut)
		restart_copy(directory / std::to_string(cut), cut);
	for (unsigned mutation = 0; mutation != 7; ++mutation)
		damaged(directory / ("damaged-" + std::to_string(mutation)), mutation);
	leased(directory / "leased");
	embedded(directory / "embedded");
}
