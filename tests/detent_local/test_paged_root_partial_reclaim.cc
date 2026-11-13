#include "detent_local/disk_store.hh"
#include "paged_disk_fixture.hh"
#include "reader_metadata.hh"
#include <completion/native_completion.h>
#include <fcntl.h>
#include <sys/file.h>
#include <source_location>

void check(bool value, std::source_location at = std::source_location::current()) {
    if (!value)
        throw std::runtime_error("partial reclamation invariant at line " + std::to_string(at.line()));
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	// The store's work runs here, as the coordinator's worker runs it; advance() settles it.
	for (unsigned turns = 0;; ++turns) {
		check(turns < 100000);
		store->advance();
		auto work = store->work();
		if (!work)
			break;
		work->run();
	}
}
l::GenerationId identity(unsigned value) {
	l::GenerationId result{};
	result[0] = 'p';
	result[14] = static_cast<char>(value >> 8);
	result[15] = static_cast<char>(value);
	return result;
}
l::DiskUsage census(const std::filesystem::path &root) {
	l::DiskUsage result;
	for (const auto &entry : std::filesystem::directory_iterator(root)) {
		result.bytes += entry.file_size();
		++result.entries;
		result.generations += entry.path().extension() == ".lease";
	}
	return result;
}
int main() {
	const auto root = std::filesystem::current_path() / ".scratch" /
					  ("paged-partial-reclaim-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{root};
	l::GenerationId generation{};
	generation.fill('g');
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::detail::PagedSnapshotBuilder builder(source, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	install(root, image);
	// What cut publications leave: generations the table does not reach, their files cut short.
	const auto bytes = std::span<const char>(image.bytes()->data(), image.bytes()->size());
	for (unsigned length = 0; length <= l::paged::root_bytes; ++length) {
		const auto id = identity(length);
		const auto name = l::generation_name(id);
		write(root / (name + ".lease"), {});
		write(root / (name + ".image"), bytes.first(length));
		const auto metadata = l::detail::encode_reader_metadata(
			{id, image.descriptor().checksum, {}, l::paged::root_bytes});
		write(root / (name + ".read"), std::span(metadata).first(std::min<std::size_t>(length, metadata.size())));
	}
	const auto pinned_name = l::generation_name(identity(159));
	const int pinned = ::open((root / (pinned_name + ".lease")).c_str(), O_RDWR | O_CLOEXEC);
	check(pinned >= 0 && ::flock(pinned, LOCK_SH | LOCK_NB) == 0);
	auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 64);
	const auto initial = account->used();
	l::DiskStoreOptions options;
	options.limits = {64 * l::paged::segment_bytes, 2000, 400};
	options.recovery_limits.retirement_domain = &domain;
	options.recovery_limits.mapping_account =
		std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
	drive(store);
	store->request_collection();
	drive(store);
	check(store->status() == l::StoreStatus::Ready && store->stats().used == census(root));
	// Collection removes every unreached generation but the one a reader holds, whatever its files hold.
	for (unsigned length = 0; length <= l::paged::root_bytes; ++length)
		for (const auto *suffix : {".lease", ".image", ".read"})
			check(std::filesystem::exists(root / (l::generation_name(identity(length)) + suffix)) == (length == 159));
	check(store->stats().used.generations == 2);
	check(::flock(pinned, LOCK_UN) == 0 && ::close(pinned) == 0);
	store->request_collection();
	drive(store);
    if (std::filesystem::exists(root / (pinned_name + ".lease"))) {
        if (store->error()) std::rethrow_exception(store->error());
        if (store->collection_error()) std::rethrow_exception(store->collection_error());
        throw std::runtime_error("unpinned partial root retained with " + std::to_string(store->stats().used.generations) + " generations");
    }

	check(store->stats().used == census(root) && store->stats().used.generations == 1);
	for (const auto &segment : image.segments())
		check(std::filesystem::exists(root /
										l::detail::SegmentNames(segment.ownership.segment).payload_name()));
	store->close();
	drive(store);
	check(store->drained());
	store.reset();
	domain.close().wait();
	check(account->used() == initial && options.recovery_limits.mapping_account->used() == 0);
}
