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
			output += fragment->bytes();
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
		for (bool compressed : {false, true}) {
			auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
			std::optional<d::DecodedFragment> retained;
			std::size_t control_capacity;
			auto one = first;
			one.final = true;
			one.total = one.bytes.size();
			auto encoded = d::encode_fragment(one, compressed);
			{
				d::FragmentDecoder charged(d::UpdateKind::Snapshot, 1024 * 1024, compressed, account);
				control_capacity = account->used();
				auto data = std::span<const char>(encoded);
				while (!data.empty()) {
					d::WorkBudget b{17, 1};
					charged.feed(data, b);
				}
				retained = charged.take();
				charged.finish();
				require(retained && retained->bytes() == one.bytes);
			}
			const auto plain_capacity = account->used();
			require(plain_capacity > one.bytes.size() + d::PagedStorage::page_overhead());
			const auto peak = account->peak();
			retained.reset();
			require(account->used() == 0);
			const auto encoded_capacity =
				encoded.size() - d::frame_header_bytes + d::PagedStorage::page_overhead();
			for (auto limit : {control_capacity + encoded_capacity - 1, peak - 1}) {
				auto small = std::make_shared<d::StorageLedger>(limit);
				{
					d::FragmentDecoder pressured(d::UpdateKind::Snapshot, 1024 * 1024, compressed, small);
					auto data = std::span<const char>(encoded);
					bool pressure = false;
					try {
						while (!data.empty()) {
							d::WorkBudget b;
							pressured.feed(data, b);
						}
					} catch (const d::StoragePressure &) {
						pressure = true;
					}
					require(pressure && small->used() <= limit);
					bool fenced = false;
					try {
						pressured.take();
					} catch (const std::invalid_argument &) {
						fenced = true;
					}
					require(fenced);
				}
				require(small->used() == 0);
			}
		}
		{
			auto probe_account = std::make_shared<d::StorageLedger>(1024 * 1024);
			auto probe = d::make_owned_buffer(probe_account, d::fragment_bytes);
			const auto payload_capacity = probe_account->used();
			std::size_t control_capacity;
			{
				d::FragmentDecoder control(d::UpdateKind::Snapshot, 1024 * 1024, false, probe_account);
				control_capacity = probe_account->used() - payload_capacity;
				require(control_capacity != 0);
			}
			auto account = std::make_shared<d::StorageLedger>(payload_capacity + control_capacity);
			probe.reset();
			std::optional<d::DecodedFragment> held;
			{
				d::FragmentDecoder decoder(d::UpdateKind::Snapshot, 1024 * 1024, false, account);
				auto initial_wire = d::encode_fragment(first);
				auto data = std::span<const char>(initial_wire);
				while (!data.empty()) {
					d::WorkBudget b;
					decoder.feed(data, b);
				}
				held = decoder.take();
				auto final_wire = d::encode_fragment(second);
				data = std::span<const char>(final_wire);
				bool pressure = false;
				try {
					while (!data.empty()) {
						d::WorkBudget b;
						decoder.feed(data, b);
					}
				} catch (const d::StoragePressure &) {
					pressure = true;
				}
				require(pressure && held && held->bytes() == first.bytes);
			}
			require(account->used() == payload_capacity);
			held.reset();
			require(account->used() == 0);
		}
		{
			auto zero = std::make_shared<d::StorageLedger>(0);
			bool rejected = false;
			try {
				d::FragmentDecoder decoder(d::UpdateKind::Snapshot, 1024, false, zero);
			} catch (const d::StoragePressure &) {
				rejected = true;
			}
			require(rejected && zero->used() == 0);
		}
		// Empty final fragments retain no payload capacity, including compressed.
		for (bool compressed : {false, true}) {
			d::Fragment empty;
			empty.update.fill('e');
			empty.final = true;
			require(decode(d::encode_fragment(empty, compressed), compressed).empty());
		}
		std::cout << "bounded framing passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
