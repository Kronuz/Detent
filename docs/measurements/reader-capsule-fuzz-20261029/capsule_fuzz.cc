#include "reader_capsule.hh"
#include "paged_root_prefix.hh"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <optional>

namespace x = detent::local::detail;
namespace p = detent::local::paged;
namespace {
struct Seed {
	std::array<char, x::reader_capsule_bytes> bytes{};
	x::ReaderMetadata binding;
	Seed() {
		const auto *path = std::getenv("DETENT_FUZZ_CAPSULE");
		if (!path) std::abort();
		std::ifstream file(path, std::ios::binary);
		file.read(bytes.data(), bytes.size());
		if (file.gcount() != bytes.size() || file.peek() != std::char_traits<char>::eof())
			std::abort();
		binding = x::decode_reader_capsule(bytes).metadata;
	}
};
const Seed &seed() { static const Seed value; return value; }
void probe(std::span<const char> bytes, std::size_t root_length) {
	std::optional<x::ReaderCapsule> decoded;
	try { decoded = x::decode_reader_capsule(bytes); } catch (const std::exception &) {}
	if (decoded) {
		const auto encoded = x::encode_reader_capsule(*decoded);
		if (!std::equal(encoded.begin(), encoded.end(), bytes.begin(), bytes.end())) std::abort();
	}
	try {
		x::validate_reader_capsule_prefix(bytes, seed().binding,
			std::span(seed().bytes).subspan(80, root_length));
	} catch (const std::exception &) {}
}
void reseal(std::span<char> bytes, std::size_t body, std::size_t digest) {
	const auto hash = p::physical_commitment(std::span<const char>(bytes).first(body));
	std::copy(hash.begin(), hash.end(), bytes.begin() + digest);
}
}
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
	const auto raw = std::span(reinterpret_cast<const char *>(data), size);
	const auto root_length = size > 1 ? ((std::size_t(data[0]) << 8) | data[1]) % 321 : size;
	probe(raw, root_length);
	auto mutated = seed().bytes;
	for (std::size_t i = 0; i + 2 < size; i += 3) {
		const auto offset = ((std::size_t(data[i]) << 8) | data[i + 1]) % mutated.size();
		mutated[offset] ^= char(data[i + 2]);
	}
	if (size && (data[0] & 1)) {
		const auto digest = p::physical_commitment(std::span(mutated).subspan(80, p::root_bytes));
		std::copy(digest.begin(), digest.end(), mutated.begin() + 448);
	}
	if (size && (data[0] & 2)) reseal(std::span(mutated).subspan(400, 192), 160, 160);
	reseal(mutated, 592, 592);
	probe(mutated, root_length);
	const auto length = size > 3 ? ((std::size_t(data[2]) << 8) | data[3]) % 625 : size % 625;
	probe(std::span(mutated).first(length), root_length);
	return 0;
}
