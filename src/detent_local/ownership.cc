#include "detent_local/read_counters.hh"
#include "detent_local/ownership.hh"
#include "detent_local/layout.hh"
#include "detent_local/paged_layout.hh"
#include <algorithm>
#include <cstring>
#include <sha256.h>
#include <stdexcept>

namespace detent::local {
namespace {
void require(bool condition) {
	if (!condition)
		throw std::invalid_argument("invalid local storage ownership record");
}
bool nonzero(const SegmentId &identity) {
	return std::any_of(identity.begin(), identity.end(), [](char value) { return value != 0; });
}
void seal(std::span<char> bytes, std::size_t prefix) {
	SHA256 hash;
	hash.add(bytes.data(), prefix);
	unsigned char digest[32];
	hash.getHash(digest);
	std::memcpy(bytes.data() + prefix, digest, sizeof digest);
}
void validate(std::span<const char> bytes, std::string_view magic, std::size_t length) {
	require(bytes.size() == length);
	require(std::equal(magic.begin(), magic.end(), bytes.begin()) && read_integer(bytes, 8, 4) == 1 &&
			read_integer(bytes, 12, 4) == length);
	DETENT_COUNT(bytes_hashed, length - 32);
	SHA256 hash;
	hash.add(bytes.data(), length - 32);
	unsigned char digest[32];
	hash.getHash(digest);
	require(std::memcmp(bytes.data() + length - 32, digest, sizeof digest) == 0);
}
} // namespace
std::array<char, segment_ownership_bytes> encode_segment_ownership(const SegmentOwnership &ownership) {
	require(nonzero(ownership.segment) && ownership.length >= paged::page_header_bytes &&
			ownership.length <= paged::segment_bytes);
	std::array<char, segment_ownership_bytes> bytes{};
	constexpr std::string_view magic = "DTSGOWN2";
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, 1, 4);
	write_integer(bytes, 12, bytes.size(), 4);
	std::copy(ownership.segment.begin(), ownership.segment.end(), bytes.begin() + 16);
	write_integer(bytes, 32, ownership.length, 8);
	std::copy(ownership.checksum.begin(), ownership.checksum.end(), bytes.begin() + 40);
	seal(bytes, 96);
	return bytes;
}
SegmentOwnership decode_segment_ownership(std::span<const char> bytes) {
	validate(bytes, "DTSGOWN2", segment_ownership_bytes);
	require(std::all_of(bytes.begin() + 72, bytes.begin() + 96, [](char value) { return value == 0; }));
	SegmentOwnership ownership;
	std::copy_n(bytes.begin() + 16, 16, ownership.segment.begin());
	ownership.length = read_integer(bytes, 32, 8);
	std::copy_n(bytes.begin() + 40, 32, ownership.checksum.begin());
	require(nonzero(ownership.segment) && ownership.length >= paged::page_header_bytes &&
			ownership.length <= paged::segment_bytes);
	return ownership;
}
} // namespace detent::local
