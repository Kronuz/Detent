#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace detent::local {
using SegmentId = std::array<char, 16>;
inline constexpr std::size_t segment_ownership_bytes = 128;
// A segment's `.owner` record: no storage identity (D8.1).
struct SegmentOwnership {
	SegmentId segment{};
	std::uint64_t length = 0;
	std::array<char, 32> checksum{};
	bool operator==(const SegmentOwnership &) const = default;
};
// Pure, fixed-layout codecs: no completion, journal or networking dependency.
std::array<char, segment_ownership_bytes> encode_segment_ownership(const SegmentOwnership &);
SegmentOwnership decode_segment_ownership(std::span<const char>);
} // namespace detent::local
