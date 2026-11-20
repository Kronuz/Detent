// libFuzzer target of the transfer receiver (dense-distribution D4): honest full-copy and delta
// transfers of packer-built generations, damaged by the input, and the input as a raw stream. A
// transfer accepted under the honest root must be the honest result byte for byte; anything else
// must be refused with TransferRefused. With DETENT_FUZZ_REPLAY, main replays seeded inputs.
#include "dense_graph_fixture.hh"
#include "detent_local/layout.hh"
#include "paged_transfer.hh"
#include <cstdlib>
#include <iostream>
#include <map>

namespace {
namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
using Fixture = dense_graph_test::Fixture;

l::SegmentId own_id(std::uint32_t number) {
	l::SegmentId id{};
	id[0] = 'r';
	l::write_integer(id, 12, number, 4);
	return id;
}
std::shared_ptr<Fixture> generation(std::size_t count, std::size_t changed) {
	auto result = std::make_shared<Fixture>();
	std::array<char, 202> key{};
	for (std::size_t i = 0; i < count; ++i) {
		detent::KnobDef value{static_cast<std::int64_t>(i == changed ? i + 7 : i)};
		if (i == 5)
			for (std::size_t k = 0; k < 90; ++k)
				value.overrides.emplace("group-" + std::string(40, char('a' + k % 26)) + std::to_string(k), std::int64_t(k));
		std::vector<char> body(p::definition_size(value));
		p::encode_definition(body, value, 8);
		const auto length = p::encode_definition_key(key, "ns:k" + std::to_string(100000 + i));
		result->rows.push_back({std::string(key.data(), length), std::move(body)});
	}
	const auto length = p::encode_namespace_key(key, "ns");
	std::vector<char> watermark(8);
	l::write_integer(watermark, 0, 9, 8);
	result->rows.push_back({std::string(key.data(), length), std::move(watermark)});
	return dense_graph_test::seal(std::move(result), count, 1);
}
l::detail::TransferPages reader(const Fixture &f) {
	return [&f](const p::Reference &r) { return std::span<const char>(f.bytes).subspan(r.offset, r.length); };
}
struct Seeds {
	std::shared_ptr<Fixture> base = generation(1500, SIZE_MAX), after = generation(1500, 700);
	std::vector<char> full, delta;
	l::detail::TransferBase on{base->root.tree, reader(*base)};
	std::vector<std::string> honest_full, honest_delta;
	Seeds() {
		std::vector<p::Reference> all, changed;
		std::map<std::string, p::Reference> by_path;
		for (const auto &page : base->pages) {
			all.push_back(page.reference);
			by_path[page.path] = page.reference;
		}
		for (const auto &page : after->pages) {
			const auto found = by_path.find(page.path);
			if (found == by_path.end() || found->second.digest != page.reference.digest)
				changed.push_back(page.reference);
		}
		full = l::detail::make_transfer(all, reader(*base));
		delta = l::detail::make_transfer(changed, reader(*after));
		honest_full = packs(receive(full, nullptr, base->root.tree.digest));
		honest_delta = packs(receive(delta, &on, after->root.tree.digest));
	}
	static l::detail::ReceivedTransfer receive(std::span<const char> stream, const l::detail::TransferBase *base,
											   const p::Digest &root) {
		return l::detail::receive_transfer(std::make_shared<d::StorageLedger>(std::size_t(64) << 20), stream, base,
										   {root, 10}, own_id, p::Limits{});
	}
	static std::vector<std::string> packs(const l::detail::ReceivedTransfer &got) {
		std::vector<std::string> out;
		for (const auto &segment : got.segments)
			out.emplace_back(segment.bytes->view());
		return out;
	}
};
const Seeds &seeds() {
	static const Seeds value;
	return value;
}
// Accepted under the honest root means the honest result; refused means TransferRefused.
void probe(std::span<const char> stream, const l::detail::TransferBase *base, const p::Digest &root,
		   const std::vector<std::string> *honest) {
	try {
		const auto got = Seeds::receive(stream, base, root);
		if (honest && Seeds::packs(got) != *honest)
			std::abort();
	} catch (const l::detail::TransferRefused &) {
	}
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
	const auto &s = seeds();
	const std::span<const char> input(reinterpret_cast<const char *>(data), size);
	probe(input, nullptr, s.base->root.tree.digest, &s.honest_full);
	probe(input, &s.on, s.after->root.tree.digest, &s.honest_delta);
	const bool use_delta = size && data[0] & 1;
	auto mutated = use_delta ? s.delta : s.full;
	for (std::size_t i = 1; i + 3 < size; i += 4) {
		const auto offset = ((std::size_t(data[i]) << 16) | (std::size_t(data[i + 1]) << 8) | data[i + 2]) % mutated.size();
		mutated[offset] ^= static_cast<char>(data[i + 3]);
	}
	const auto &root = use_delta ? s.after->root.tree.digest : s.base->root.tree.digest;
	const auto *base = use_delta ? &s.on : nullptr;
	const auto *honest = use_delta ? &s.honest_delta : &s.honest_full;
	probe(mutated, base, root, honest);
	if (size > 1)
		mutated.resize(mutated.size() - (data[size - 1] * 257u + size) % mutated.size());
	probe(mutated, base, root, honest);
	return 0;
}

#ifdef DETENT_FUZZ_REPLAY
int main(int argc, char **argv) {
	const unsigned rounds = argc > 1 ? unsigned(std::stoul(argv[1])) : 300;
	std::uint64_t state = 0x7472616e73666572;
	std::vector<std::uint8_t> input;
	for (unsigned round = 0; round < rounds; ++round) {
		input.resize(1 + round % 61);
		for (auto &byte : input) {
			state = state * 6364136223846793005ull + 1442695040888963407ull;
			byte = std::uint8_t(state >> 56);
		}
		LLVMFuzzerTestOneInput(input.data(), input.size());
	}
	std::cout << "paged transfer fuzz replay: " << rounds << " inputs\n";
	return 0;
}
#endif
