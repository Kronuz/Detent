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
void validate_reader_capsule_prefix(std::span<const char> bytes, const ReaderMetadata &binding,
									std::span<const char> original_root) {
	require(bytes.size() <= reader_capsule_bytes && original_root.size() <= paged::root_bytes);
	const auto matches = [&](std::size_t offset, std::span<const char> expected) {
		if (bytes.size() > offset) {
			const auto available =
				bytes.subspan(offset, std::min(expected.size(), bytes.size() - offset));
			require(std::equal(available.begin(), available.end(), expected.begin()));
		}
	};
	std::array<char, 16> framing{'D', 'T', 'C', 'A', 'P', 'S', '0', '1'};
	write_integer(framing, 8, 1, 4);
	write_integer(framing, 12, reader_capsule_bytes, 4);
	matches(0, framing);
	matches(16, encode_storage_identity(binding.storage));
	if (bytes.size() > 80) {
		const auto root_bytes =
			bytes.subspan(80, std::min<std::size_t>(paged::root_bytes, bytes.size() - 80));
		validate_paged_root_prefix(root_bytes);
		matches(80, original_root);
		if (root_bytes.size() == paged::root_bytes) {
			require(paged::physical_commitment(root_bytes) == binding.root_checksum);
			const auto root = paged::decode_root(root_bytes, paged_format_limits());
			if (bytes.size() >= 488)
				require(read_integer(bytes, 480, 4) + read_integer(bytes, 484, 4) == root.records);
			if (bytes.size() >= 512) {
				const auto physical = read_integer(bytes, 504, 8);
				require(physical >= paged::root_bytes &&
						root.bytes <= physical - paged::root_bytes);
			}
		}
	}
	if (bytes.size() > 400)
		validate_reader_metadata_prefix(
			bytes.subspan(400, std::min<std::size_t>(reader_metadata_bytes, bytes.size() - 400)),
			binding);
	if (bytes.size() > 592) {
		const auto digest = paged::physical_commitment(bytes.first(592));
		require(std::equal(bytes.begin() + 592, bytes.end(), digest.begin()));
	}
	if (bytes.size() == reader_capsule_bytes)
		decode_reader_capsule(bytes);
}
} // namespace detent::local::detail
