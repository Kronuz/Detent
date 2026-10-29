#pragma once
#include "reader_metadata.hh"

namespace detent::local::detail {
inline constexpr std::size_t reader_capsule_bytes = 624;
struct ReaderCapsule {
	StorageIdentity storage{};
	paged::Root root;
	ReaderMetadata metadata;
};
std::array<char, reader_capsule_bytes> encode_reader_capsule(const ReaderCapsule &);
ReaderCapsule decode_reader_capsule(std::span<const char>);
} // namespace detent::local::detail
