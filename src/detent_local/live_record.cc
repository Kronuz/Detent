#include "live_record.hh"
#include "detent_local/layout.hh"
#include <algorithm>
#include <cstring>
#include <sha256.h>
#include <stdexcept>

namespace detent::local::detail {
namespace {
constexpr std::string_view magic = "DTLIVE01";
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid live record");
}
template <class Bytes> bool nonzero(const Bytes &value) {
	return std::any_of(value.begin(), value.end(), [](char byte) { return byte != 0; });
}
std::array<char, 32> digest(std::span<const char> bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<char, 32> out{};
	hash.getHash(reinterpret_cast<unsigned char *>(out.data()));
	return out;
}
} // namespace
void encode_live_record(const LiveRecord &record, std::span<char> out) {
	require(out.size() == live_record_size(record.rows.size()) && record.rows.size() <= UINT32_MAX &&
			nonzero(record.storage) && nonzero(record.generation) && nonzero(record.root_checksum) &&
			nonzero(record.read_digest));
	std::fill(out.begin(), out.end(), char(0));
	std::copy(magic.begin(), magic.end(), out.begin());
	write_integer(out, 8, 1, 4);
	write_integer(out, 12, out.size(), 4);
	std::copy(record.storage.begin(), record.storage.end(), out.begin() + 16);
	std::copy(record.generation.begin(), record.generation.end(), out.begin() + 32);
	std::copy(record.root_checksum.begin(), record.root_checksum.end(), out.begin() + 48);
	std::copy(record.read_digest.begin(), record.read_digest.end(), out.begin() + 80);
	write_integer(out, 112, record.rows.size(), 4);
	for (std::size_t i = 0; i < record.histogram.size(); ++i)
		write_integer(out, 120 + i * 4, record.histogram[i], 4);
	for (std::size_t i = 0; i < record.rows.size(); ++i) {
		const auto &row = record.rows[i];
		require(nonzero(row.segment) && row.length && row.live.pages && row.live.bytes <= row.length &&
				(i == 0 || std::memcmp(record.rows[i - 1].segment.data(), row.segment.data(), 16) < 0));
		const auto at = live_header_bytes + i * live_row_bytes;
		std::copy(row.segment.begin(), row.segment.end(), out.begin() + at);
		write_integer(out, at + 16, row.length, 8);
		std::copy(row.checksum.begin(), row.checksum.end(), out.begin() + at + 24);
		write_integer(out, at + 56, row.live.pages, 8);
		write_integer(out, at + 64, row.live.bytes, 8);
	}
	const auto sum = digest(out.first(out.size() - live_digest_bytes));
	std::copy(sum.begin(), sum.end(), out.end() - live_digest_bytes);
}
LiveRecord decode_live_record(std::span<const char> bytes, std::uint64_t maximum_rows) {
	require(bytes.size() >= live_header_bytes + live_digest_bytes && std::string_view(bytes.data(), 8) == magic &&
			read_integer(bytes, 8, 4) == 1 && read_integer(bytes, 12, 4) == bytes.size());
	const auto rows = read_integer(bytes, 112, 4);
	require(rows <= maximum_rows && bytes.size() == live_record_size(rows) && read_integer(bytes, 116, 4) == 0 &&
			std::all_of(bytes.begin() + 1148, bytes.begin() + live_header_bytes,
						[](char byte) { return byte == 0; }));
	const auto sum = digest(bytes.first(bytes.size() - live_digest_bytes));
	require(std::equal(sum.begin(), sum.end(), bytes.end() - live_digest_bytes));
	LiveRecord record;
	std::copy_n(bytes.begin() + 16, 16, record.storage.begin());
	std::copy_n(bytes.begin() + 32, 16, record.generation.begin());
	std::copy_n(bytes.begin() + 48, 32, record.root_checksum.begin());
	std::copy_n(bytes.begin() + 80, 32, record.read_digest.begin());
	require(nonzero(record.storage) && nonzero(record.generation) && nonzero(record.root_checksum) &&
			nonzero(record.read_digest));
	for (std::size_t i = 0; i < record.histogram.size(); ++i)
		record.histogram[i] = static_cast<std::uint32_t>(read_integer(bytes, 120 + i * 4, 4));
	record.rows.resize(rows);
	for (std::size_t i = 0; i < rows; ++i) {
		auto &row = record.rows[i];
		const auto at = live_header_bytes + i * live_row_bytes;
		std::copy_n(bytes.begin() + at, 16, row.segment.begin());
		row.length = read_integer(bytes, at + 16, 8);
		std::copy_n(bytes.begin() + at + 24, 32, row.checksum.begin());
		row.live = {read_integer(bytes, at + 56, 8), read_integer(bytes, at + 64, 8)};
		require(nonzero(row.segment) && row.length && row.length <= paged::segment_bytes && row.live.pages &&
				row.live.bytes <= row.length &&
				(i == 0 || std::memcmp(record.rows[i - 1].segment.data(), row.segment.data(), 16) < 0));
	}
	return record;
}
} // namespace detent::local::detail
