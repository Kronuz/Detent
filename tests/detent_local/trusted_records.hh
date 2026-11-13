#pragma once
// Test support: installs a hand-built generation as the store's one namespace, the files a publication
// leaves (docs/status/namespace-s7.md). A paged generation's `.read` and `.live` come from one full
// recovery of what was installed, the way a publication derives them from its plan.
#include "detent_distribution/retirement.hh"
#include "detent_local/derived_ids.hh"
#include "detent_local/generation.hh"
#include "detent_local/ownership.hh"
#include "detent_local/store_table.hh"
#include "leased_paged_root.hh"
#include "live_record.hh"
#include "names.hh"
#include "paged_snapshot_builder.hh"
#include "prepared_reader.hh"
#include "reader_metadata.hh"
#include "recovery_context.hh"
#include "segment_names.hh"
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sha256.h>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::local::detail {
struct RecoveredTesting {
	static const LeasedPagedRoot &root(const RecoveredGeneration &g) { return *g.paged_; }
};
} // namespace detent::local::detail

namespace trusted_records {
struct Segment {
	detent::local::SegmentOwnership ownership;
	std::span<const char> bytes;
};
inline void put(const std::filesystem::path &path, std::span<const char> bytes) {
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		throw std::runtime_error("cannot write " + path.string());
	std::size_t at = 0;
	while (at < bytes.size()) {
		const auto n = ::write(fd, bytes.data() + at, bytes.size() - at);
		if (n <= 0)
			throw std::runtime_error("short write " + path.string());
		at += std::size_t(n);
	}
	::close(fd);
}
inline std::vector<char> slurp(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
inline std::array<char, 32> sha(std::span<const char> bytes) {
	class SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<unsigned char, 32> digest{};
	hash.getHash(digest.data());
	std::array<char, 32> out{};
	std::memcpy(out.data(), digest.data(), 32);
	return out;
}
// Writes the table naming `descriptor` as the one namespace, replaced by rename so a reader sees the old
// table or the new one. The entry binds `read`'s digest (empty for a flat image or a staged generation).
inline void select(const std::filesystem::path &directory, const detent::local::Descriptor &descriptor,
				   std::span<const char> read = {}) {
	namespace l = detent::local;
	l::StoreTable table;
	table.sequence = 1;
	table.profile = descriptor.profile;
	l::TableEntry entry;
	entry.name = l::namespace_name(descriptor.cursor);
	entry.cursor = l::namespace_cursor(descriptor.cursor);
	entry.generation = descriptor.generation;
	entry.ordinal = descriptor.ordinal;
	entry.root_length = descriptor.length;
	entry.root = descriptor.checksum;
	entry.read = sha(read);
	table.entries.push_back(entry);
	const auto bytes = l::encode_table(table);
	put(directory / ".tmp-selected", bytes);
	std::filesystem::rename(directory / ".tmp-selected", directory / "selected");
}
// Writes a generation's `.image`, empty `.lease` and segments, and selects it: what recovery reads.
inline void stage(const std::filesystem::path &directory, const detent::local::Descriptor &descriptor,
				  std::span<const char> image, std::span<const Segment> segments = {}) {
	namespace l = detent::local;
	namespace x = l::detail;
	const auto id = descriptor.generation;
	put(directory / std::string(x::GenerationName(id, x::GenerationFile::Image).view()), image);
	put(directory / std::string(x::GenerationName(id, x::GenerationFile::Lease).view()), {});
	for (const auto &segment : segments) {
		const x::SegmentNames names(segment.ownership.segment);
		put(directory / names.owner_name(), l::encode_segment_ownership(segment.ownership));
		put(directory / names.payload_name(), segment.bytes);
	}
	select(directory, descriptor);
}
// The `.read` and `.live` of the generation `directory` selects, from one full recovery.
inline void derive(const std::filesystem::path &directory, const detent::local::Descriptor &descriptor,
				   detent::local::ViewLimits limits) {
	namespace d = detent::distribution;
	namespace l = detent::local;
	namespace x = l::detail;
	auto heap = std::make_shared<d::StorageLedger>(1ull << 30);
	auto mappings = std::make_shared<d::StorageLedger>(1ull << 30);
	d::RetirementRuntime runtime(heap, 1);
	auto domain = runtime.domain(heap, 16384);
	{
		limits.retirement_domain = &domain;
		limits.mapping_account = mappings;
		x::RecoveryContext context{domain.factory(), heap, mappings, limits, {}, {}};
		auto generation = x::RecoveryEngine::recover(directory, context);
		const auto &root = x::RecoveredTesting::root(generation);
		const auto &registry = *root.registry();
		const auto metadata = x::encode_reader_metadata(x::PreparedReader::describe(generation));
		x::LiveRecord live;
		live.generation = descriptor.generation;
		live.root_checksum = descriptor.checksum;
		live.read_digest = x::reader_metadata_digest(metadata);
		std::copy(registry.override_histogram().begin(), registry.override_histogram().end(),
				  live.histogram.begin());
		for (std::size_t i = 0; i < registry.inputs().size(); ++i) {
			const auto &ownership = registry.inputs()[i].ownership;
			live.rows.push_back({ownership.segment, ownership.length, ownership.checksum, registry.reachability(i)});
		}
		std::vector<char> bytes(x::live_record_size(live.rows.size()));
		x::encode_live_record(live, bytes);
		const auto id = descriptor.generation;
		put(directory / std::string(x::GenerationName(id, x::GenerationFile::ReaderMetadata).view()), metadata);
		put(directory / std::string(x::GenerationName(id, x::GenerationFile::Live).view()), bytes);
		select(directory, descriptor, metadata);
	}
	domain.close().wait();
}
// Writes `directory` as the store a publication of `descriptor` leaves: `owner.lock`, the table, the
// generation's `.image`, `.lease`, `.read` and `.live`, and every segment's pack and owner record.
inline void install(const std::filesystem::path &directory, const detent::local::Descriptor &descriptor,
					std::span<const char> image, std::span<const Segment> segments = {},
					detent::local::ViewLimits limits = {}) {
	namespace l = detent::local;
	namespace x = l::detail;
	std::filesystem::create_directories(directory);
	if (::chmod(directory.c_str(), 0700) != 0)
		throw std::runtime_error("cannot chmod " + directory.string());
	put(directory / "owner.lock", {});
	stage(directory, descriptor, image, segments);
	if (image.size() < 8 || std::memcmp(image.data(), "DTROOT03", 8) != 0) {
		// A flat image: an empty `.read` and a `.live` without rows.
		x::LiveRecord live;
		live.generation = descriptor.generation;
		live.root_checksum = descriptor.checksum;
		live.read_digest = sha({});
		std::vector<char> bytes(x::live_record_size(0));
		x::encode_live_record(live, bytes);
		const auto id = descriptor.generation;
		put(directory / std::string(x::GenerationName(id, x::GenerationFile::ReaderMetadata).view()), {});
		put(directory / std::string(x::GenerationName(id, x::GenerationFile::Live).view()), bytes);
		return;
	}
	try {
		derive(directory, descriptor, limits);
	} catch (const std::exception &error) {
		throw std::runtime_error("install: cannot derive the records of " + directory.string() + ": " +
								 error.what());
	}
}
// A built paged image as the store's one namespace.
inline void install(const std::filesystem::path &directory, const detent::local::detail::PagedImage &image,
					detent::local::ViewLimits limits = {}) {
	std::vector<Segment> segments;
	for (const auto &s : image.segments())
		segments.push_back({s.ownership, s.bytes->view()});
	install(directory, image.descriptor(), image.bytes()->view(), segments, limits);
}
} // namespace trusted_records
