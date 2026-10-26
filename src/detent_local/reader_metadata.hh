#pragma once
#include "detent_local/generation.hh"
#include "paged_graph_validator.hh"

namespace detent::local::detail {
inline constexpr std::size_t reader_metadata_bytes = prepared_metadata_bytes;
struct ReaderMetadata {
	StorageIdentity storage{};
	GenerationId generation{};
	paged::Digest root_checksum{};
	PagedStateCounts counts;
	std::uint64_t physical_bytes = 0;
};
// Ownership and the descriptor binding authenticate the publisher's statistics.
std::array<char, reader_metadata_bytes> encode_reader_metadata(const ReaderMetadata &);
ReaderMetadata decode_reader_metadata(std::span<const char>);
void validate_reader_metadata_prefix(std::span<const char>, const ReaderMetadata &binding);
} // namespace detent::local::detail
