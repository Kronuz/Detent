#pragma once
#include "detent_distribution/frame_hash_counts.hh"
#include "detent_local/paged_layout.hh"

namespace detent::delivery {
// What a tier hashed, from the process-wide counters (a tier is a process; a test that runs two in one takes
// differences). `built` is the part of `hashed` that writing its own generations required (page digests
// and pack checksums); everything else, `checked_bytes`, is re-validation, first touch and metadata.
struct HashStats {
	local::paged::HashCounts hashed;
	local::paged::BuiltHashCounts built;
	distribution::FrameHashCounts frame;
	std::uint64_t built_bytes() const noexcept { return built.page_bytes + built.pack_bytes; }
	std::uint64_t checked_bytes() const noexcept {
		return hashed.page_bytes + hashed.pack_bytes + hashed.metadata_bytes - built_bytes();
	}
	std::uint64_t total_bytes() const noexcept {
		return hashed.page_bytes + hashed.pack_bytes + hashed.metadata_bytes + frame.bytes;
	}
	friend HashStats operator-(const HashStats &a, const HashStats &b) noexcept {
		return {a.hashed - b.hashed, a.built - b.built,
				{a.frame.calls - b.frame.calls, a.frame.bytes - b.frame.bytes}};
	}
};
inline HashStats hash_stats() noexcept {
	return {local::paged::hash_counts(), local::paged::built_hash_counts(), distribution::frame_hash_counts()};
}
} // namespace detent::delivery
