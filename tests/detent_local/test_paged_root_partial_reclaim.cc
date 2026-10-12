#include "detent_local/disk_store.hh"
#include "paged_disk_fixture.hh"
#include <completion/native_completion.h>
#include <fcntl.h>
#include <sys/file.h>

void drive(const std::shared_ptr<l::DiskStore> &store) {
	for (unsigned turns = 0;; ++turns) {
		require(turns < 1000000);
		store->advance();
		auto job = store->operation();
		if (!job)
			break;
		job->submitted();
		require(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
	}
}
l::StorageIdentity identity(unsigned value) {
	l::StorageIdentity result{};
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
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	install(root, image, storage);
	write(root / "owner.lock", {});
	const auto bytes = std::span<const char>(image.bytes()->data(), image.bytes()->size());
	for (unsigned length = 0; length <= l::paged::root_bytes; ++length) {
		const auto id = identity(length);
		const auto name = l::generation_name(id);
		write(root / (name + ".lease"),
			  l::encode_generation_ownership(
				  {storage, id, image.descriptor().length, image.descriptor().checksum, 2}));
		write(root / (name + ".image"), bytes.first(length));
	}
	const auto pinned_name = l::generation_name(identity(159));
	const int pinned = ::open((root / (pinned_name + ".lease")).c_str(), O_RDWR | O_CLOEXEC);
	require(pinned >= 0 && ::flock(pinned, LOCK_SH | LOCK_NB) == 0);
	for (unsigned variant = 0; variant < 2; ++variant) {
		const auto id = identity(400 + variant);
		const auto name = l::generation_name(id);
		write(root / (name + ".lease"),
			  l::encode_generation_ownership(
				  {storage, id, image.descriptor().length, image.descriptor().checksum, 2}));
		std::array<char, l::paged::root_bytes> invalid{};
		std::copy(bytes.begin(), bytes.end(), invalid.begin());
		invalid[variant ? 0 : 56] ^= 1;
		write(root / (name + ".image"), std::span<const char>(invalid).first(variant ? 80 : 320));
	}
	auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 64);
	const auto initial = account->used();
	l::DiskStoreOptions options;
	options.limits = {64 * l::paged::segment_bytes, 1000, 400};
	options.recovery_limits.retirement_domain = &domain;
	options.recovery_limits.mapping_account =
		std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
	drive(store);
	store->request_collection();
	drive(store);
	require(store->status() == l::StoreStatus::Ready && store->stats().used == census(root));
	for (unsigned length = 0; length <= l::paged::root_bytes; ++length)
		require(std::filesystem::exists(root / (l::generation_name(identity(length)) + ".lease")) ==
				(length == 159));
	for (unsigned variant = 0; variant < 2; ++variant)
		require(std::filesystem::exists(root / (l::generation_name(identity(400 + variant)) + ".lease")));
	require(store->stats().used.generations == 4);
	require(::flock(pinned, LOCK_UN) == 0 && ::close(pinned) == 0);
	store->request_collection();
	drive(store);
	require(!std::filesystem::exists(root / (pinned_name + ".lease")));
	require(store->stats().used == census(root) && store->stats().used.generations == 3);
	for (const auto &segment : image.segments())
		require(std::filesystem::exists(root /
										l::detail::SegmentNames(segment.ownership.segment).payload_name()));
	store->close();
	drive(store);
	require(store->drained());
	store.reset();
	domain.close().wait();
	require(account->used() == initial && options.recovery_limits.mapping_account->used() == 0);
}
