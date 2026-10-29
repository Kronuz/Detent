#include "reader_capsule.hh"
#include "paged_root_prefix.hh"
#include <algorithm>
#include <stdexcept>

namespace detent::local::detail {
namespace {
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid prepared-reader capsule");
}
void validate(const ReaderCapsule &value, std::span<const char> image) {
	const auto &metadata = value.metadata;
	require(metadata.storage == value.storage &&
			metadata.root_checksum == paged::physical_commitment(image) &&
			std::uint64_t(metadata.counts.bools) + metadata.counts.ints == value.root.records &&
			metadata.physical_bytes >= paged::root_bytes &&
			value.root.bytes <= metadata.physical_bytes - paged::root_bytes);
}
} // namespace
std::array<char, reader_capsule_bytes> encode_reader_capsule(const ReaderCapsule &value) {
	const auto storage = encode_storage_identity(value.storage);
	const auto root = paged::encode_root(value.root, paged_format_limits());
	const auto metadata = encode_reader_metadata(value.metadata);
	validate(value, root);
	std::array<char, reader_capsule_bytes> bytes{};
	constexpr std::string_view magic = "DTCAPS01";
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, 1, 4);
	write_integer(bytes, 12, bytes.size(), 4);
	std::copy(storage.begin(), storage.end(), bytes.begin() + 16);
	std::copy(root.begin(), root.end(), bytes.begin() + 80);
	std::copy(metadata.begin(), metadata.end(), bytes.begin() + 400);
	const auto digest = paged::physical_commitment(std::span(bytes).first(592));
	std::copy(digest.begin(), digest.end(), bytes.begin() + 592);
	return bytes;
}
ReaderCapsule decode_reader_capsule(std::span<const char> bytes) {
	require(bytes.size() == reader_capsule_bytes &&
			std::string_view(bytes.data(), 8) == "DTCAPS01" && read_integer(bytes, 8, 4) == 1 &&
			read_integer(bytes, 12, 4) == reader_capsule_bytes);
	const auto digest = paged::physical_commitment(bytes.first(592));
	require(std::equal(digest.begin(), digest.end(), bytes.begin() + 592));
	const auto image = bytes.subspan(80, paged::root_bytes);
	ReaderCapsule value{decode_storage_identity(bytes.subspan(16, storage_identity_bytes)),
						paged::decode_root(image, paged_format_limits()),
						decode_reader_metadata(bytes.subspan(400, reader_metadata_bytes))};
	validate(value, image);
	return value;
}
} // namespace detent::local::detail
