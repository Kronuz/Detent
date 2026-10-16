#include "../../src/detent_delivery/response_probe_internal.hh"
#include "probe_fixture.hh"
#include <exception>
#include <iostream>
#include <type_traits>

namespace d = detent::distribution;
namespace t = detent::delivery;
namespace p = t::detail;
using namespace probe_fixture;
namespace {
static_assert(!std::is_convertible_v<p::ProbeOffer, t::ReceivedUpdate>);
template <typename Function>
void rejects(Function function, std::source_location where = std::source_location::current()) {
	bool refused = false;
	try {
		function();
	} catch (const std::invalid_argument &) {
		refused = true;
	}
	check(refused, where);
}
void drain(t::ResponseDecoder &decoder) {
	for (unsigned turn = 0; turn != 100000; ++turn) {
		d::WorkBudget budget{8192, 8};
		if (decoder.discard(budget))
			return;
	}
	throw std::runtime_error("probe disposal did not drain");
}
void consume(t::ResponseDecoder &decoder, std::string_view wire, std::size_t quantum) {
	auto bytes = std::span<const char>(wire.data(), wire.size());
	for (unsigned turn = 0; !bytes.empty() || !decoder.done(); ++turn) {
		check(turn < 2000000);
		d::WorkBudget budget{quantum, 2};
		decoder.feed(bytes, budget);
		if (bytes.empty())
			decoder.eof();
	}
}
p::ProbeOffer decode(const Wire &wire, bool compressed, bool reference = false,
					std::size_t quantum = 8192, std::uint64_t maximum = 16 * 1024 * 1024) {
	auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
	d::Limits limits;
	limits.bytes = maximum;
	limits.storage_account = account;
	std::optional<p::ProbeOffer> result;
	std::exception_ptr failure;
	{
		const std::array<std::string_view, 1> names{"ns"};
		t::ResponseDecoder decoder(cursor(), cursor(4), d::NamespaceSelection(names), compressed, limits,
			{t::SessionResponseBinding{t::SessionResponsePurpose::Next, session(), 1, true}});
		try {
			p::ProbeDecoderAccess::prepare(decoder, reference);
			auto untouched = std::span<const char>(wire.bytes.data(), wire.bytes.size());
			d::WorkBudget zero{0, 0};
			decoder.feed(untouched, zero);
			check(untouched.size() == wire.bytes.size() && !decoder.done());
			consume(decoder, wire.bytes, quantum);
			rejects([&] { (void)decoder.take(); });
			result = p::ProbeDecoderAccess::take(decoder);
			rejects([&] { (void)p::ProbeDecoderAccess::take(decoder); });
		} catch (...) {
			failure = std::current_exception();
		}
		drain(decoder);
	}
	check(account->used() == 0 && account->peak() < 512 * 1024);
	if (failure)
		std::rethrow_exception(failure);
	check(result.has_value());
	return *result;
}
void semantic_response(const Wire &wire) {
	const std::array<std::string_view, 1> names{"ns"};
	t::ResponseDecoder decoder(cursor(), cursor(4), d::NamespaceSelection(names), false);
	std::exception_ptr failure;
	try {
		consume(decoder, wire.bytes, 8192);
	} catch (...) {
		failure = std::current_exception();
	}
	drain(decoder);
	if (failure)
		std::rethrow_exception(failure);
}
} // namespace
int main() {
	try {
		for (const auto kind : {d::UpdateKind::Snapshot, d::UpdateKind::Delta, d::UpdateKind::Progress}) {
			const auto payload = semantic(kind);
			for (const bool compressed : {false, true})
				for (const bool chunked : {false, true})
					for (const bool reference : {false, true}) {
						const auto wire = response(payload, compressed, chunked, 17, 'u', kind);
						const auto offer = decode(wire, compressed, reference, 1);
						check(offer.target == cursor() && offer.window == 1 && offer.kind == kind &&
							  offer.logical_bytes == payload.size() && offer.wire_bytes == wire.body_bytes &&
							  offer.digest == digest(payload));
					}
		}
		std::string large(4 * 1024 * 1024, 'x');
		for (std::size_t i = 0; i != large.size(); ++i)
			large[i] = static_cast<char>((i * 17 + i / 257) & 255);
		for (const bool compressed : {false, true}) {
			const auto offer = decode(response(large, compressed, false, 65536, compressed ? 'b' : 'a'),
									 compressed);
			check(offer.logical_bytes == large.size() && offer.digest == digest(large));
		}
		const auto invalid_semantics = response("not a profile-1 message", false);
		check(decode(invalid_semantics, false).digest == digest("not a profile-1 message"));
		rejects([&] { (void)decode(invalid_semantics, false, true); });
		rejects([&] { (void)decode(response(semantic(d::UpdateKind::Snapshot, 6), false), false, true); });
		rejects([&] { semantic_response(response(semantic(d::UpdateKind::Snapshot, 6), false)); });
		for (unsigned fault = 0; fault != 8; ++fault) {
			auto wire = response(semantic(), false);
			if (fault == 0)
				wire.bytes.back() ^= 1;
			else if (fault == 1)
				wire.bytes.pop_back();
			else if (fault == 2)
				wire.bytes += 'x';
			else if (fault == 3)
				replace(wire.bytes, "X-Detent-Window: 1", "X-Detent-Window: 2");
			else if (fault == 4)
				replace(wire.bytes, session_hex(), "62626262626262626262626262626262");
			else if (fault == 5)
				replace(wire.bytes, "X-Detent-Profile: 1", "X-Detent-Profile: 2");
			else if (fault == 6)
				replace(wire.bytes, "X-Detent-Revision: 5", "X-Detent-Revision: 3");
			else
				replace(wire.bytes, "X-Detent-Coding: raw", "X-Detent-Coding: zstd");
			rejects([&] { (void)decode(wire, false); });
		}
		rejects([&] { (void)decode(response(large, false), false, false, 8192, 1024); });
		std::cout << "probe framing, reference semantics, bounded workspace and disposal passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
