#pragma once
#include "detent_local/generation.hh"
#include "detent_local/ownership.hh"
#include "detent_local/paged_layout.hh"
#include "paged_graph_validator.hh"
#include <array>
#include <span>
#include <vector>

namespace detent::local::detail {
// `generation-<id>.live`: what a recovery walk derives from a generation's pages and a trusted restart
// cannot recompute without reading them. The build that verified the generation writes it before the
// table that reaches the generation; docs/mechanisms/trusted-restart.md has the byte layout.
inline constexpr std::size_t live_header_bytes = 1152, live_row_bytes = 72, live_digest_bytes = 32;
struct LiveRow {
	SegmentId segment{};
	std::uint64_t length = 0;
	std::array<char, 32> checksum{};
	SegmentReachability live;
};
// Bytes 16-31 of the record are reserved and zero: the record holds nothing host-specific.
struct LiveRecord {
	GenerationId generation{};
	paged::Digest root_checksum{}, read_digest{};
	std::array<std::uint32_t, 257> histogram{};
	std::vector<LiveRow> rows; // strictly ascending by segment id
};
constexpr std::size_t live_record_size(std::size_t rows) noexcept {
	return live_header_bytes + rows * live_row_bytes + live_digest_bytes;
}
// `out` must be exactly live_record_size(record.rows.size()) bytes.
void encode_live_record(const LiveRecord &, std::span<char> out);
// Throws std::invalid_argument on any structural or digest failure; `maximum_rows` bounds the allocation.
LiveRecord decode_live_record(std::span<const char>, std::uint64_t maximum_rows);
} // namespace detent::local::detail
