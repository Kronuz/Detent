#include "detent_distribution/framing.hh"
#include <iostream>
#include <source_location>
namespace d = detent::distribution;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("owned frame invariant at " + std::to_string(where.line()));
}
std::shared_ptr<const d::OwnedBuffer> body(std::shared_ptr<d::StorageLedger> account, std::string_view text) {
	auto output = d::make_owned_buffer(account, text.size());
	output->append(text);
	output->seal();
	return output;
}
void drain(d::OwnedFragmentEncoder &encoder) {
	unsigned turns = 0;
	while (true) {
		d::WorkBudget budget{1, 1};
		if (encoder.discard(budget))
			break;
		check(++turns < 2000000);
	}
}
std::shared_ptr<const d::OwnedBuffer> encode(d::OwnedFragmentEncoder &encoder) {
	unsigned turns = 0;
	while (!encoder.ready()) {
		d::WorkBudget budget{1, 1};
		encoder.step(budget);
		check(++turns < 2000000);
	}
	return encoder.take();
}
std::string decode(std::string_view wire, d::UpdateKind kind, bool compression) {
	d::FragmentDecoder decoder(kind, 1024 * 1024, compression);
	std::span<const char> input(wire.data(), wire.size());
	std::string result;
	while (!input.empty()) {
		d::WorkBudget budget{17, 1};
		decoder.feed(input, budget);
		if (auto fragment = decoder.take())
			result += fragment->bytes();
	}
	decoder.finish();
	return result;
}
} // namespace
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
		d::Identity identity;
		identity.fill('u');
		const std::string text(65536, 'x');
		for (bool compressed : {false, true}) {
			for (auto kind : {d::UpdateKind::Snapshot, d::UpdateKind::Delta, d::UpdateKind::Progress}) {
				d::OwnedFragmentEncoder encoder(kind, identity, 65539, compressed, account);
				encoder.begin(body(account, text), false);
				auto first = encode(encoder);
				check(first->sealed() && first->size() <= d::OwnedFragmentEncoder::maximum_wire_bytes());
				encoder.begin(body(account, "end"), true);
				auto last = encode(encoder);
				std::string wire(first->view());
				wire += last->view();
				check(decode(wire, kind, compressed) == text + "end");
				if (!compressed) {
					d::Fragment expected{kind, identity, 0, 65539, false, text};
					check(first->view() == d::encode_fragment(expected));
					expected.offset = 65536;
					expected.final = true;
					expected.bytes = "end";
					check(last->view() == d::encode_fragment(expected));
				} else
					check(first->size() < first->storage_capacity());
				const auto pinned = account->used();
				drain(encoder);
				check(first->view() == std::string_view(wire).substr(0, first->size()));
				if (compressed)
					check(account->used() < pinned);
				first.reset();
				last.reset();
			}
			check(account->used() == 0);
			{
				d::OwnedFragmentEncoder empty(d::UpdateKind::Snapshot, identity, 0, compressed, account);
				empty.begin(body(account, ""), true);
				auto packet = encode(empty);
				check(decode(packet->view(), d::UpdateKind::Snapshot, compressed).empty());
				drain(empty);
			}
			check(account->used() == 0);
		}
		{
			d::OwnedFragmentEncoder encoder(d::UpdateKind::Snapshot, identity, 3, false, account);
			auto input = d::make_owned_buffer(account, 3);
			input->append("abc");
			bool refused = false;
			try {
				encoder.begin(input, true);
			} catch (const std::invalid_argument &) {
				refused = true;
			}
			check(refused);
			input->seal();
			encoder.begin(input, true);
			const auto before = account->used();
			d::WorkBudget zero{0, 0};
			encoder.step(zero);
			check(!encoder.ready() && account->used() == before);
			drain(encoder);
		}
		check(account->used() == 0);
		for (std::size_t limit : {1u, 1000u, 66000u}) {
			auto tiny = std::make_shared<d::StorageLedger>(limit);
			bool refused = false;
			try {
				d::OwnedFragmentEncoder encoder(d::UpdateKind::Snapshot, identity, 3, true, tiny);
			} catch (const std::bad_alloc &) {
				refused = true;
			} catch (const d::StoragePressure &) {
				refused = true;
			}
			check(refused && tiny->used() == 0);
		}
		std::cout << "owned raw vectors, static zstd, tiny budgets, multiple kinds, pinned output and "
					 "refusal passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
