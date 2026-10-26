#include "reader_metadata.hh"
#include <algorithm>
#include <limits>
#include <sha256.h>
#include <stdexcept>

namespace detent::local::detail {
namespace {
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid prepared-reader metadata");
}
template <class Bytes> bool nonzero(const Bytes &value) {
	return std::any_of(value.begin(), value.end(), [](char byte) { return byte != 0; });
}
void validate(const ReaderMetadata &value) {
	const auto &counts = value.counts;
	require(nonzero(value.storage) && nonzero(value.generation) && nonzero(value.root_checksum) &&
	        std::uint64_t(counts.bools) + counts.ints <= UINT32_MAX && counts.maximum_overrides <= 256 &&
	        (counts.overrides == 0) == (counts.maximum_overrides == 0) &&
	        counts.overrides <= std::uint64_t(counts.bools) * counts.maximum_overrides &&
	        value.physical_bytes >= paged::root_bytes);
}
paged::Digest checksum(std::span<const char> bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<unsigned char, 32> digest{};
	hash.getHash(digest.data());
	paged::Digest result;
	std::copy(digest.begin(), digest.end(), result.begin());
	return result;
}
} // namespace
std::array<char, reader_metadata_bytes> encode_reader_metadata(const ReaderMetadata &value) {
	validate(value);
	std::array<char, reader_metadata_bytes> bytes{};
	constexpr std::string_view magic = "DTREAD01";
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, 1, 4);
	write_integer(bytes, 12, bytes.size(), 4);
	std::copy(value.storage.begin(), value.storage.end(), bytes.begin() + 16);
	std::copy(value.generation.begin(), value.generation.end(), bytes.begin() + 32);
	std::copy(value.root_checksum.begin(), value.root_checksum.end(), bytes.begin() + 48);
	write_integer(bytes, 80, value.counts.bools, 4);
	write_integer(bytes, 84, value.counts.ints, 4);
	write_integer(bytes, 88, value.counts.overrides, 8);
	write_integer(bytes, 96, value.counts.strings, 8);
	write_integer(bytes, 104, value.physical_bytes, 8);
	write_integer(bytes, 112, value.counts.maximum_overrides, 4);
	write_integer(bytes, 116, 1, 4);
	const auto digest = checksum(std::span(bytes).first(160));
	std::copy(digest.begin(), digest.end(), bytes.begin() + 160);
	return bytes;
}
void validate_reader_metadata_prefix(std::span<const char> bytes, const ReaderMetadata &binding) {
	require(bytes.size() <= reader_metadata_bytes);
	const auto expected = encode_reader_metadata(binding);
	auto matches = [&](std::size_t begin, std::size_t end) {
		if (bytes.size() > begin)
			require(std::equal(bytes.begin() + begin, bytes.begin() + std::min(end, bytes.size()),
			                   expected.begin() + begin));
	};
	matches(0, 80);
	matches(116, 160);
	if (bytes.size() >= 88)
		require(read_integer(bytes, 80, 4) + read_integer(bytes, 84, 4) <= UINT32_MAX);
	if (bytes.size() >= 96)
		require(read_integer(bytes, 88, 8) <= read_integer(bytes, 80, 4) * 256);
	if (bytes.size() >= 112)
		require(read_integer(bytes, 104, 8) >= paged::root_bytes);
	if (bytes.size() >= 116) {
		auto value = binding;
		value.counts = {static_cast<std::uint32_t>(read_integer(bytes, 80, 4)),
		                static_cast<std::uint32_t>(read_integer(bytes, 84, 4)), read_integer(bytes, 88, 8),
		                read_integer(bytes, 96, 8), static_cast<std::uint32_t>(read_integer(bytes, 112, 4))};
		value.physical_bytes = read_integer(bytes, 104, 8);
		validate(value);
	}
	if (bytes.size() > 160) {
		const auto digest = checksum(bytes.first(160));
		require(std::equal(bytes.begin() + 160, bytes.end(), digest.begin()));
	}
}
ReaderMetadata decode_reader_metadata(std::span<const char> bytes) {
	require(bytes.size() == reader_metadata_bytes && std::string_view(bytes.data(), 8) == "DTREAD01" &&
	        read_integer(bytes, 8, 4) == 1 && read_integer(bytes, 12, 4) == reader_metadata_bytes &&
	        read_integer(bytes, 116, 4) == 1 &&
	        std::all_of(bytes.begin() + 120, bytes.begin() + 160, [](char byte) { return byte == 0; }));
	const auto digest = checksum(bytes.first(160));
	require(std::equal(digest.begin(), digest.end(), bytes.begin() + 160));
	ReaderMetadata value;
	std::copy_n(bytes.begin() + 16, value.storage.size(), value.storage.begin());
	std::copy_n(bytes.begin() + 32, value.generation.size(), value.generation.begin());
	std::copy_n(bytes.begin() + 48, value.root_checksum.size(), value.root_checksum.begin());
	value.counts.bools = read_integer(bytes, 80, 4);
	value.counts.ints = read_integer(bytes, 84, 4);
	value.counts.overrides = read_integer(bytes, 88, 8);
	value.counts.strings = read_integer(bytes, 96, 8);
	value.physical_bytes = read_integer(bytes, 104, 8);
	value.counts.maximum_overrides = read_integer(bytes, 112, 4);
	validate(value);
	return value;
}
} // namespace detent::local::detail
