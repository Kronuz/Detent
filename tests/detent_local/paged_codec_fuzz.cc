#include "dense_fixture.hh"
#include <cstdlib>
namespace {
using namespace dense_test;
void walk(std::span<const char> bytes, std::span<const std::uint8_t> input) {
	std::size_t offset = p::page_header_bytes;
	for (std::size_t visited = 0; visited <= 202; ++visited) {
		const auto node = p::decode_node(bytes, offset);
		if (node.kind != p::PageKind::Branch)
			return;
		const auto child = input.empty() ? 0 : input[visited % input.size()] % node.labels.size();
		const auto slot = node.slot(child);
		if (slot & 0x8000) {
			p::decode_external(bytes, slot & 0x7fff);
			return;
		}
		if (slot <= offset)
			throw std::invalid_argument("nonprogressing dense slot");
		offset = slot;
	}
	throw std::invalid_argument("dense walk depth");
}
void probe(std::span<const char> bytes, std::span<const std::uint8_t> input) {
	try {
		const auto root = p::decode_root(bytes);
		if (!std::ranges::equal(p::encode_root(root), bytes))
			std::abort();
	} catch (const std::invalid_argument &) {
	}
	try {
		const auto ref = p::decode_reference(bytes);
		if (!std::ranges::equal(p::encode_reference(ref), bytes))
			std::abort();
	} catch (const std::invalid_argument &) {
	}
	try {
		const auto ref = reference(bytes);
		p::verify_page(bytes, ref);
		walk(bytes, input);
	} catch (const std::invalid_argument &) {
	}
}
const std::vector<char> &branch() {
	static const auto seed = external_branch();
	return seed;
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
	using namespace dense_test;
	const std::span<const std::uint8_t> input(data, size);
	probe({reinterpret_cast<const char *>(data), size}, input);
	auto mutated = size && data[0] & 1 ? branch() : watermark();
	for (std::size_t i = 1; i + 2 < size; i += 3) {
		const auto offset = ((std::size_t(data[i]) << 8) | data[i + 1]) % mutated.size();
		mutated[offset] ^= static_cast<char>(data[i + 2]);
	}
	try {
		reseal(mutated);
	} catch (const std::invalid_argument &) {
	}
	probe(mutated, input);
	mutated.resize(size % (mutated.size() + 1));
	probe(mutated, input);
	return 0;
}
