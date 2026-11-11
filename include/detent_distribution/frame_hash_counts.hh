#pragma once
#include <atomic>
#include <cstdint>

namespace detent::distribution {
// Bytes this process fed to the frame SHA-256 (the digest in every fragment header), encoding or decoding.
// Relaxed process-wide counters: a tier is a process, so these are that tier's frame hashing.
struct FrameHashCounts {
	std::uint64_t calls = 0, bytes = 0;
	friend bool operator==(const FrameHashCounts &, const FrameHashCounts &) = default;
};
namespace detail {
inline std::atomic<std::uint64_t> frame_hash_calls{0}, frame_hash_bytes{0};
} // namespace detail
inline void count_frame_hash(std::uint64_t bytes) noexcept {
	detail::frame_hash_calls.fetch_add(1, std::memory_order_relaxed);
	detail::frame_hash_bytes.fetch_add(bytes, std::memory_order_relaxed);
}
inline FrameHashCounts frame_hash_counts() noexcept {
	return {detail::frame_hash_calls.load(std::memory_order_relaxed),
			detail::frame_hash_bytes.load(std::memory_order_relaxed)};
}
} // namespace detent::distribution
