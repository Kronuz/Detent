#include "detent_distribution/framing.hh"
#include <iostream>
namespace d = detent::distribution;
void require(bool value) {
	if (!value)
		throw std::runtime_error("framing invariant failed");
}
std::string decode(std::string_view wire, bool compressed) {
	d::FragmentDecoder decoder(d::UpdateKind::Snapshot, 1024 * 1024, compressed);
	auto input = std::span(wire.data(), wire.size());
	std::string output;
	while (!input.empty()) {
		d::WorkBudget budget{7, 1};
		decoder.feed(input, budget);
		if (auto fragment = decoder.take())
			output += fragment->bytes;
	}
	decoder.finish();
	return output;
}
int main() {
	try {
		d::Fragment first;
		first.update.fill('u');
		first.total = 65539;
		first.bytes.assign(65536, 'x');
		auto second = first;
		second.offset = first.bytes.size();
		second.final = true;
		second.bytes = "end";
		for (bool compressed : {false, true}) {
			auto first_wire = d::encode_fragment(first, compressed),
				 second_wire = d::encode_fragment(second, compressed);
			require(decode(first_wire + second_wire, compressed) == first.bytes + second.bytes);
			for (std::size_t offset : {0u, 8u, 9u, 10u, 12u, 16u, 20u, 24u, 40u, 48u, 56u, 64u, 95u, 96u}) {
				auto broken = first_wire + second_wire;
				broken[offset] ^= 0x7f;
				bool rejected = false;
				try {
					decode(broken, compressed);
				} catch (const std::exception &) {
					rejected = true;
				}
				require(rejected);
			}
			for (auto broken :
				 {first_wire, first_wire + second_wire.substr(0, second_wire.size() - 1),
				  second_wire + first_wire, first_wire + first_wire, first_wire + second_wire + "x"}) {
				bool rejected = false;
				try {
					decode(broken, compressed);
				} catch (const std::exception &) {
					rejected = true;
				}
				require(rejected);
			}
		}
		bool rejected = false;
		try {
			decode(d::encode_fragment(first, true) + d::encode_fragment(second, true), false);
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		d::FragmentDecoder pending(d::UpdateKind::Snapshot, 1024 * 1024);
		auto wire = d::encode_fragment(first);
		auto input = std::span<const char>(wire.data(), wire.size());
		while (!input.empty()) {
			d::WorkBudget budget;
			pending.feed(input, budget);
		}
		auto more = std::span<const char>(wire.data(), wire.size());
		d::WorkBudget budget;
		pending.feed(more, budget);
		require(more.size() == wire.size());
		require(pending.take().has_value());
		std::cout << "bounded framing passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
