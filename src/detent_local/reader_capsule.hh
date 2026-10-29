#pragma once
#include "reader_metadata.hh"

namespace detent::local::detail {
inline constexpr std::size_t reader_capsule_bytes = prepared_capsule_bytes;
struct ReaderCapsule {
	StorageIdentity storage{};
	paged::Root root;
	ReaderMetadata metadata;
};
std::array<char, reader_capsule_bytes> encode_reader_capsule(const ReaderCapsule &);
ReaderCapsule decode_reader_capsule(std::span<const char>);
// Prefix structure supplements an existing exclusive lease and original-file proof.
void validate_reader_capsule_prefix(std::span<const char>, const ReaderMetadata &binding,
                                   std::span<const char> original_root = {});
} // namespace detent::local::detail
