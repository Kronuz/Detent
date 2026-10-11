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
bool nonzero(const StorageIdentity &identity) {
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
	SHA256 hash;
	hash.add(bytes.data(), length - 32);
	unsigned char digest[32];
	hash.getHash(digest);
	require(std::memcmp(bytes.data() + length - 32, digest, sizeof digest) == 0);
}
} // namespace
std::array<char, storage_identity_bytes> encode_storage_identity(const StorageIdentity &identity) {
	require(nonzero(identity));
	std::array<char, storage_identity_bytes> bytes{};
	constexpr std::string_view magic = "DTSTORE1";
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, 1, 4);
	write_integer(bytes, 12, bytes.size(), 4);
	std::copy(identity.begin(), identity.end(), bytes.begin() + 16);
	seal(bytes, 32);
	return bytes;
}
StorageIdentity decode_storage_identity(std::span<const char> bytes) {
	validate(bytes, "DTSTORE1", storage_identity_bytes);
	StorageIdentity identity;
	std::copy_n(bytes.begin() + 16, identity.size(), identity.begin());
	require(nonzero(identity));
	return identity;
}
std::array<char, generation_ownership_bytes>
encode_generation_ownership(const GenerationOwnership &ownership) {
	// Admission quotas are absent; older owned generations can exceed them.
	require(nonzero(ownership.storage) && nonzero(ownership.generation) &&
			((ownership.layout_version == 1 && ownership.image_length >= header_bytes) ||
			 (ownership.layout_version == 2 && ownership.image_length == paged::root_bytes)));
	std::array<char, generation_ownership_bytes> bytes{};
	constexpr std::string_view magic = "DTLEASE1";
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, 1, 4);
	write_integer(bytes, 12, bytes.size(), 4);
	std::copy(ownership.storage.begin(), ownership.storage.end(), bytes.begin() + 16);
	std::copy(ownership.generation.begin(), ownership.generation.end(), bytes.begin() + 32);
	write_integer(bytes, 48, ownership.image_length, 8);
	write_integer(bytes, 56, ownership.layout_version, 4);
	write_integer(bytes, 60, 1, 4);
	std::copy(ownership.image_checksum.begin(), ownership.image_checksum.end(), bytes.begin() + 64);
	seal(bytes, 96);
	return bytes;
}
GenerationOwnership decode_generation_ownership(std::span<const char> bytes) {
	validate(bytes, "DTLEASE1", generation_ownership_bytes);
	require(read_integer(bytes, 60, 4) == 1);
	GenerationOwnership ownership;
	std::copy_n(bytes.begin() + 16, 16, ownership.storage.begin());
	std::copy_n(bytes.begin() + 32, 16, ownership.generation.begin());
	ownership.image_length = read_integer(bytes, 48, 8);
	ownership.layout_version = read_integer(bytes, 56, 4);
	std::copy_n(bytes.begin() + 64, 32, ownership.image_checksum.begin());
	require(nonzero(ownership.storage) && nonzero(ownership.generation) &&
			((ownership.layout_version == 1 && ownership.image_length >= header_bytes) ||
			 (ownership.layout_version == 2 && ownership.image_length == paged::root_bytes)));
	return ownership;
}
std::array<char, segment_ownership_bytes> encode_segment_ownership(const SegmentOwnership &ownership) {
	require(nonzero(ownership.storage) && nonzero(ownership.segment) &&
			ownership.length >= paged::page_header_bytes && ownership.length <= paged::segment_bytes);
	std::array<char, segment_ownership_bytes> bytes{};
	constexpr std::string_view magic = "DTSGOWN1";
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, 1, 4);
	write_integer(bytes, 12, bytes.size(), 4);
	std::copy(ownership.storage.begin(), ownership.storage.end(), bytes.begin() + 16);
	std::copy(ownership.segment.begin(), ownership.segment.end(), bytes.begin() + 32);
	write_integer(bytes, 48, ownership.length, 8);
	write_integer(bytes, 56, 2, 4);
	std::copy(ownership.checksum.begin(), ownership.checksum.end(), bytes.begin() + 64);
	seal(bytes, 96);
	return bytes;
}
SegmentOwnership decode_segment_ownership(std::span<const char> bytes) {
	validate(bytes, "DTSGOWN1", segment_ownership_bytes);
	require(read_integer(bytes, 56, 4) == 2 && !read_integer(bytes, 60, 4));
	SegmentOwnership ownership;
	std::copy_n(bytes.begin() + 16, 16, ownership.storage.begin());
	std::copy_n(bytes.begin() + 32, 16, ownership.segment.begin());
	ownership.length = read_integer(bytes, 48, 8);
	std::copy_n(bytes.begin() + 64, 32, ownership.checksum.begin());
	require(nonzero(ownership.storage) && nonzero(ownership.segment) &&
			ownership.length >= paged::page_header_bytes && ownership.length <= paged::segment_bytes);
	return ownership;
}
} // namespace detent::local
