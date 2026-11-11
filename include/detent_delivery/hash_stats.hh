#pragma once
#include "detent_distribution/frame_hash_counts.hh"
#include "detent_local/paged_layout.hh"

namespace detent::delivery {
// What a tier hashed, from the process-wide counters (a tier is a process; a test that runs two in one takes
// differences). `built` is what writing its own generations required (page digests and pack checksums);
// `checked` is everything else: re-validation, first touch and metadata. Each counter only grows.
struct HashStats {
	local::paged::HashCounts checked;
	local::paged::BuiltHashCounts built;
	distribution::FrameHashCounts frame;
	local::paged::HashCounts hashed() const noexcept { return checked + built; }
	std::uint64_t built_bytes() const noexcept { return built.page_bytes + built.pack_bytes; }
	std::uint64_t checked_bytes() const noexcept {
		return checked.page_bytes + checked.pack_bytes + checked.metadata_bytes;
	}
	std::uint64_t total_bytes() const noexcept { return built_bytes() + checked_bytes() + frame.bytes; }
	friend HashStats operator-(const HashStats &a, const HashStats &b) noexcept {
		return {a.checked - b.checked, a.built - b.built,
				{a.frame.calls - b.frame.calls, a.frame.bytes - b.frame.bytes}};
	}
};
inline HashStats hash_stats() noexcept {
	return {local::paged::checked_hash_counts(), local::paged::built_hash_counts(),
			distribution::frame_hash_counts()};
}
} // namespace detent::delivery
