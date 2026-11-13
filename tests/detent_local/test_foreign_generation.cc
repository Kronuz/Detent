// A selected generation the store cannot read (another format's framing, damage) fences the store at
// startup and keeps every file: D43 has no migration, and nothing unreadable authorizes a removal.
#include "detent_local/disk_store.hh"
#include "generation_mapping.hh"
#include "lazy_page_access.hh"
#include "paged_disk_fixture.hh"
#include <cstring>
#include <fcntl.h>
#include <source_location>
#include <sys/file.h>

void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("foreign generation invariant at " + std::to_string(where.line()));
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	for (unsigned turns = 0;; ++turns) {
		check(turns < 100000);
		store->advance();
		auto work = store->work();
		if (!work)
			break;
		work->run();
	}
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
void close(const std::shared_ptr<l::DiskStore> &store) {
	store->close();
	drive(store);
	check(store->drained());
}
std::vector<char> read(const std::filesystem::path &path) { return trusted_records::slurp(path); }
std::vector<std::filesystem::path> listing(const std::filesystem::path &directory) {
	std::vector<std::filesystem::path> names;
	for (const auto &entry : std::filesystem::directory_iterator(directory))
		names.push_back(entry.path().filename());
	std::sort(names.begin(), names.end());
	return names;
}
constexpr std::uint32_t foreign_layout = 9;
// A store whose selected generation carries another format's framing in its root and every pack's
// first page. Every checksum matches.
void foreign_install(const std::filesystem::path &directory) {
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::GenerationId generation{};
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	const auto image = builder.take();
	install(directory, image);
	const auto name = l::generation_name(generation);
	auto root = read(directory / (name + ".image"));
	root[7] = static_cast<char>('0' + foreign_layout);
	l::write_integer(root, 8, foreign_layout, 4);
	write(directory / (name + ".image"), root);
	auto descriptor = image.descriptor();
	descriptor.checksum = l::paged::metadata_commitment(root);
	trusted_records::select(directory, descriptor, read(directory / (name + ".read")));
	for (const auto &segment : image.segments()) {
		const l::detail::SegmentNames names(segment.ownership.segment);
		auto pack = read(directory / names.payload_name());
		pack[7] = static_cast<char>('0' + foreign_layout);
		write(directory / names.payload_name(), pack);
		write(directory / names.owner_name(),
			  l::encode_segment_ownership({segment.ownership.segment, pack.size(),
										   l::paged::metadata_commitment(pack)}));
	}
}
void refused(const std::filesystem::path &directory, unsigned mutation) {
	foreign_install(directory);
	l::GenerationId id;
	id.fill('g');
	const auto image_path = directory / (l::generation_name(id) + ".image");
	const auto lease_path = directory / (l::generation_name(id) + ".lease");
	if (mutation == 1) {
		auto bytes = read(image_path);
		bytes[20] ^= 1; // A root that differs from its table entry is damage.
		write(image_path, bytes);
	} else if (mutation == 2) {
		write(lease_path, std::string_view("x")); // A `.lease` that is not empty is damage.
	} else if (mutation == 3) {
		// The owner lock held by another writer refuses the store before it reads anything.
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
	auto before = listing(directory);
	Runtime runtime;
	auto store = runtime.open(directory);
	drive(store);
	check(store->status() == l::StoreStatus::Fenced && store->error() && read(directory / "selected") == selected);
	close(store);
	// The agent store's own names (`change`) may appear; nothing of the generation goes.
	auto after = listing(directory);
	std::erase(after, std::filesystem::path("change"));
	std::erase(before, std::filesystem::path("change"));
	check(after == before);
}
void embedded(const std::filesystem::path &directory) {
	foreign_install(directory);
	std::vector<char> page;
	for (const auto &entry : std::filesystem::directory_iterator(directory)) {
		if (entry.path().extension() == ".pack") {
			page = read(entry.path());
			page.resize(l::read_integer(page, 8, 4));
			break;
		}
	}
	check(page.size() >= l::paged::minimum_page_bytes);
	bool refused = false;
	try {
		l::paged::decode_page_header(page, l::paged::Limits{});
	} catch (const std::invalid_argument &) {
		refused = true;
	}
	check(refused);
	Runtime runtime;
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::GenerationId generation{};
	l::SegmentId segment{};
	generation.fill('v');
	segment.fill('p');
	l::detail::PagedSnapshotBuilder builder(source, generation);
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
	const trusted_records::Segment packed{{segment, page.size(), l::paged::metadata_commitment(page)}, page};
	trusted_records::stage(directory, descriptor, bytes, {&packed, 1});
	refused = false;
	try {
		l::recover_generation(directory, runtime.options.recovery_limits);
	} catch (const std::invalid_argument &) {
		refused = true;
	}
	check(refused && l::read_generation_selection(directory).descriptor == descriptor);
	{
		auto held = std::make_shared<l::detail::RecoveryFile>(
			::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
		check(held->fd >= 0);
		auto pages = runtime.domain.make<l::detail::LazyPageAccess>(held, held->fd, ::getuid(), root, runtime.options.recovery_limits,
			runtime.domain.factory(), runtime.heap, runtime.mappings);
		refused = false;
		try {
			pages->resolve(root.tree);
		} catch (const l::detail::ReadSourceFailure &) {
			refused = true;
		}
		check(refused && !pages->available() && pages->page_verifications() == 0);
	}
	// A page cannot name its format: another framing is damage, fenced at startup, its files kept.
	auto store = runtime.open(directory);
	drive(store);
	check(store->status() == l::StoreStatus::Fenced &&
		  l::read_generation_selection(directory).descriptor == descriptor);
	close(store);
}
int main() {
	const auto directory = std::filesystem::current_path() / ".scratch" /
		("foreign-generation-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{directory};
	for (unsigned mutation = 0; mutation != 4; ++mutation)
		refused(directory / ("refused-" + std::to_string(mutation)), mutation);
	embedded(directory / "embedded");
	std::cout << "foreign framing, damage, a held owner lock and an embedded foreign page refused, files kept\n";
}
