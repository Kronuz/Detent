#include "trusted_restart.hh"
#include "generation_file_access.hh"
#include "live_record.hh"
#include "names.hh"
#include "reader_metadata.hh"
#include "segment_names.hh"
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <vector>

namespace detent::local::detail {
namespace {
void require(bool value) {
	if (!value)
		throw std::runtime_error("invalid trusted-restart record");
}
// Opens `name` read-only as a record owned by the store's account, no longer than `maximum`; returns the
// descriptor (or -1 with errno from the open) and the file's size.
int open_record(int directory, std::string_view name, uid_t owner, std::uint64_t maximum, std::uint64_t &size) {
	std::array<char, 65> terminated{};
	require(name.size() < terminated.size());
	std::copy(name.begin(), name.end(), terminated.begin());
	const int fd = ::openat(directory, terminated.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return fd;
	RecoveryFile guard(fd);
	struct stat info{};
	require(::fstat(fd, &info) == 0 && info.st_size >= 0 && std::uint64_t(info.st_size) <= maximum);
	size = static_cast<std::uint64_t>(info.st_size);
	check_generation_file(fd, owner, size);
	guard.fd = -1;
	return fd;
}
} // namespace
TrustedGeneration
TrustedRestart::open(int directory, uid_t owner, const Descriptor &descriptor, const paged::Root &root,
					 StorageIdentity storage, paged::Limits bounds, const ViewLimits &limits,
					 distribution::RetirementFactory factory, std::shared_ptr<distribution::StorageLedger> heap,
					 std::shared_ptr<distribution::StorageLedger> mappings, RecoveryMetrics *metrics) {
	std::uint64_t live_size = 0;
	const GenerationName live_name(descriptor.generation, GenerationFile::Live);
	RecoveryFile live_file(open_record(directory, live_name.view(), owner, live_record_size(root.segments), live_size));
	// A missing or wrong record refuses the store; it is repaired like a corrupt one.
	require(live_file.fd >= 0);
	std::uint64_t read_size = 0;
	const GenerationName read_name(descriptor.generation, GenerationFile::ReaderMetadata);
	RecoveryFile read_file(open_record(directory, read_name.view(), owner, reader_metadata_bytes, read_size));
	require(read_file.fd >= 0 && read_size == reader_metadata_bytes);
	std::array<char, reader_metadata_bytes> read_bytes{};
	read_generation_record(read_file.fd, read_bytes);
	const auto metadata = decode_reader_metadata(read_bytes);
	require(metadata.storage == storage && metadata.generation == descriptor.generation &&
			metadata.root_checksum == descriptor.checksum &&
			std::uint64_t(metadata.counts.bools) + metadata.counts.ints == root.records &&
			metadata.physical_bytes >= paged::root_bytes && root.bytes <= metadata.physical_bytes - paged::root_bytes &&
			metadata.physical_bytes <= limits.bytes && metadata.counts.strings <= limits.strings &&
			metadata.counts.maximum_overrides <= limits.overrides && root.records <= limits.records &&
			root.namespaces <= limits.namespaces && root.pages <= limits.pages && root.segments <= limits.segments);
	// The record's bytes, its decoded rows and the reachability copy are admitted for the open's duration.
	const auto charge = live_size + root.segments * (sizeof(LiveRow) + sizeof(SegmentReachability));
	auto ticket = heap->admit(charge);
	std::vector<char> live_bytes(live_size);
	read_generation_record(live_file.fd, live_bytes);
	const auto live = decode_live_record(live_bytes, root.segments);
	require(live.generation == descriptor.generation &&
			live.root_checksum == descriptor.checksum && live.rows.size() == root.segments &&
			std::equal(live.read_digest.begin(), live.read_digest.end(),
					   read_bytes.begin() + reader_metadata_bytes - live_digest_bytes));
	std::uint64_t physical = paged::root_bytes;
	for (const auto &row : live.rows) {
		require(row.length <= metadata.physical_bytes - physical);
		physical += row.length;
	}
	require(physical == metadata.physical_bytes);
	// What the rows and the histogram derive must add up to what the root and `.read` authenticate.
	std::uint64_t pages = 0, live_bytes_total = 0;
	for (const auto &row : live.rows) {
		require(row.live.pages <= root.pages - pages && row.live.bytes <= root.bytes - live_bytes_total);
		pages += row.live.pages;
		live_bytes_total += row.live.bytes;
	}
	std::uint64_t records = 0, overrides = 0, highest = 0;
	for (std::size_t bucket = 0; bucket < live.histogram.size(); ++bucket) {
		records += live.histogram[bucket];
		overrides += bucket * std::uint64_t(live.histogram[bucket]);
		if (live.histogram[bucket])
			highest = bucket;
	}
	require(pages == root.pages && live_bytes_total == root.bytes && records == root.records &&
			overrides == metadata.counts.overrides && highest == metadata.counts.maximum_overrides);
	// The tree's first page lives in one of the rows' segments.
	if (root.pages)
		require(std::binary_search(live.rows.begin(), live.rows.end(), root.tree.segment,
								   [](const auto &row_or_id, const auto &other) {
									   constexpr auto id = [](const auto &value) -> const char * {
										   if constexpr (std::is_same_v<std::decay_t<decltype(value)>, LiveRow>)
											   return value.segment.data();
										   else
											   return value.data();
									   };
									   return std::memcmp(id(row_or_id), id(other), 16) < 0;
								   }));
	bounds.segments = root.segments;
	auto registry = factory.make<SegmentRegistry>(heap, storage, bounds);
	std::vector<SegmentReachability> reachable;
	reachable.reserve(live.rows.size());
	for (const auto &row : live.rows) {
		const SegmentNames names(row.segment);
		RecoveryFile payload(
			::openat(directory, names.payload.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
		require(payload.fd >= 0);
		auto mapping =
			factory.make<SegmentMapping>(mappings, payload.fd, owner,
										 SegmentOwnership{storage, row.segment, row.length, row.checksum});
		mapping->require_first_touch(heap);
		registry->append(std::move(mapping));
		reachable.push_back(row.live);
		if (metrics)
			++metrics->segment_mapping_installs;
	}
	registry->seal();
	registry->adopt_derived(root, reachable, live.histogram);
	if (metrics)
		metrics->trusted = true;
	return TrustedGeneration{std::move(registry), metadata.counts};
}
} // namespace detent::local::detail
