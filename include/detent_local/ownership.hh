#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace detent::local {
using StorageIdentity = std::array<char, 16>;
inline constexpr std::size_t storage_identity_bytes = 64, generation_ownership_bytes = 128;
inline constexpr std::size_t segment_ownership_bytes = 128;
struct SegmentOwnership {
	StorageIdentity storage{}, segment{};
	std::uint64_t length = 0;
	std::array<char, 32> checksum{};
	bool operator==(const SegmentOwnership &) const = default;
};
struct GenerationOwnership {
	StorageIdentity storage{}, generation{};
	std::uint64_t image_length = 0;
	std::array<char, 32> image_checksum{};
	std::uint32_t layout_version = 1;
	bool operator==(const GenerationOwnership &) const = default;
};
// Pure, fixed-layout codecs: no completion, journal or networking dependency.
std::array<char, storage_identity_bytes> encode_storage_identity(const StorageIdentity &identity);
StorageIdentity decode_storage_identity(std::span<const char> bytes);
std::array<char, generation_ownership_bytes>
encode_generation_ownership(const GenerationOwnership &ownership);
GenerationOwnership decode_generation_ownership(std::span<const char> bytes);
std::array<char, segment_ownership_bytes> encode_segment_ownership(const SegmentOwnership &);
SegmentOwnership decode_segment_ownership(std::span<const char>);
} // namespace detent::local
