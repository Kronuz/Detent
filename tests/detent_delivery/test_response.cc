#include "detent_delivery/response.hh"
#include "detent_distribution/messagepack.hh"
#include <charconv>
#include <iostream>
#include <source_location>
namespace d = detent::distribution;
namespace t = detent::delivery;
namespace m = d::messagepack;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("delivery response invariant at " + std::to_string(where.line()));
}
d::Cursor cursor(std::uint64_t revision = 5) {
	d::Cursor result;
	result.cluster.fill('c');
	result.group.fill('g');
	result.restore_epoch.fill('e');
	result.scope.fill('s');
	result.revision = revision;
	return result;
}
void put_cursor(std::string &out, d::Cursor value) {
	m::array(out, 5);
	for (const auto *part : {&value.cluster, &value.group, &value.restore_epoch})
		m::string(out, {part->data(), part->size()}, true);
	m::string(out, {value.scope.data(), value.scope.size()}, true);
	m::unsigned_integer(out, value.revision);
}
std::string message(d::UpdateKind kind, unsigned rows = 1) {
	std::string out;
	m::array(out, kind == d::UpdateKind::Progress ? 5 : 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, static_cast<unsigned>(kind));
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	if (kind != d::UpdateKind::Snapshot)
		put_cursor(out, cursor(4));
	put_cursor(out, cursor());
	if (kind == d::UpdateKind::Progress)
		return out;
	m::array(out, 4);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, rows);
	m::unsigned_integer(out, 0);
	m::unsigned_integer(out, 2 + rows * 12);
	if (kind == d::UpdateKind::Snapshot) {
		m::array(out, 1);
		m::array(out, 2);
		m::string(out, "ns");
		m::unsigned_integer(out, 5);
		m::array(out, rows);
	} else {
		m::array(out, 1);
		m::array(out, 3);
		m::unsigned_integer(out, 5);
		m::array(out, 1);
		m::array(out, 2);
		m::string(out, "ns");
		m::unsigned_integer(out, 5);
		m::array(out, rows);
	}
	for (unsigned row = 0; row < rows; ++row) {
		std::string key = std::to_string(row);
		key = "ns:k" + std::string(8 - key.size(), '0') + key;
		m::array(out, 4);
		m::unsigned_integer(out, 0);
		m::string(out, key);
		m::unsigned_integer(out, kind == d::UpdateKind::Snapshot ? 5 : 0);
		m::array(out, 2);
		m::unsigned_integer(out, 1);
		m::signed_integer(out, 7);
	}
	return out;
}
std::string response(d::UpdateKind kind, bool compressed = false, bool chunked = false, unsigned rows = 1) {
	const auto bytes = message(kind, rows);
	d::Identity update;
	update.fill('u');
	std::string body;
	for (std::size_t offset = 0; offset < bytes.size();) {
		const auto n = std::min(d::fragment_bytes, bytes.size() - offset);
		body += d::encode_fragment(
			{kind, update, offset, bytes.size(), offset + n == bytes.size(), bytes.substr(offset, n)},
			compressed);
		offset += n;
	}
	std::string head =
		"HTTP/1.1 200 OK\r\nContent-Type: application/vnd.detent.distribution\r\nX-Detent-Profile: "
		"1\r\nX-Detent-Revision: 5\r\nX-Detent-Kind: " +
		std::to_string(static_cast<unsigned>(kind)) +
		"\r\nX-Detent-Coding: " + (compressed ? "zstd" : "raw") + "\r\n";
	if (!chunked)
		return head + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
	head += "Transfer-Encoding: chunked\r\n\r\n";
	for (std::size_t offset = 0; offset < body.size();) {
		const auto n = std::min<std::size_t>(137, body.size() - offset);
		char hex[32];
		auto value = std::to_chars(hex, hex + sizeof(hex), n, 16);
		head.append(hex, value.ptr);
		head += "\r\n";
		head.append(body, offset, n);
		head += "\r\n";
		offset += n;
	}
	return head + "0\r\n\r\n";
}
void drain(t::ResponseDecoder &decoder) {
	unsigned turns = 0;
	for (;;) {
		d::WorkBudget work{8192, 8};
		if (decoder.discard(work))
			return;
		check(++turns < 1000000);
	}
}
void consume(t::ResponseDecoder &decoder, std::string_view wire, std::size_t quantum = 1) {
	auto input = std::span<const char>(wire.data(), wire.size());
	unsigned turns = 0;
	while (!input.empty() || !decoder.done()) {
		d::WorkBudget work{quantum, 2};
		decoder.feed(input, work);
		check(++turns < 20000000);
		if (input.empty())
			decoder.eof();
	}
}
void replace(std::string &text, std::string_view before, std::string_view after) {
	const auto at = text.find(before);
	check(at != std::string::npos);
	text.replace(at, before.size(), after);
}
} // namespace
int main() {
	try {
		const std::array<std::string_view, 1> names{"ns"};
		for (auto kind : {d::UpdateKind::Snapshot, d::UpdateKind::Delta, d::UpdateKind::Progress}) {
			for (bool compressed : {false, true})
				for (bool chunked : {false, true}) {
					auto account = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
					d::Limits limits;
					limits.storage_account = account;
					{
						t::ResponseDecoder decoder(cursor(),
												   kind == d::UpdateKind::Snapshot ? std::nullopt
																				   : std::optional{cursor(4)},
												   d::NamespaceSelection(names), compressed, limits);
						auto wire = response(kind, compressed, chunked);
						auto zero_input = std::span<const char>(wire.data(), wire.size());
						d::WorkBudget zero{0, 0};
						decoder.feed(zero_input, zero);
						check(zero_input.size() == wire.size() && !decoder.done());
						consume(decoder, wire);
						auto result = decoder.take();
						check(result.kind == kind && result.target == cursor());
						if (result.snapshot)
							check(result.snapshot->records().size() == 1);
						else
							check(result.delta->transactions().size() ==
								  (kind == d::UpdateKind::Progress ? 0 : 1));
						drain(decoder);
						check(account->used() > 0);
						if (result.snapshot) {
							for (;;) {
								d::WorkBudget work;
								if (result.snapshot->discard(work))
									break;
							}
						}
						if (result.delta) {
							for (;;) {
								d::WorkBudget work;
								if (result.delta->discard(work))
									break;
							}
						}
					}
					check(account->used() == 0);
				}
		}
		for (unsigned fault = 0; fault < 9; ++fault) {
			auto wire = response(d::UpdateKind::Snapshot);
			if (fault == 0)
				replace(wire, "X-Detent-Revision: 5", "X-Detent-Revision: 5\r\nx-detent-revision: 5");
			if (fault == 1)
				replace(wire, "application/vnd.detent.distribution", "application/json");
			if (fault == 2)
				replace(wire, "X-Detent-Profile: 1", "X-Detent-Profile: 2");
			if (fault == 3)
				replace(wire, "X-Detent-Revision: 5", "X-Detent-Revision: 6");
			if (fault == 4)
				wire += 'x';
			if (fault == 5)
				wire.resize(wire.size() - 1);
			if (fault == 6)
				replace(wire, "200 OK", "503 Unavailable");
			if (fault == 7)
				replace(wire, "X-Detent-Kind: 1", "X-Detent-Kind: 3");
			if (fault == 8)
				replace(wire, "X-Detent-Coding: raw", "X-Detent-Coding: zstd");
			auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
			d::Limits limits;
			limits.storage_account = account;
			{
				t::ResponseDecoder decoder(cursor(), {}, d::NamespaceSelection(names), fault == 8, limits);
				bool rejected = false;
				try {
					consume(decoder, wire, 17);
				} catch (const std::invalid_argument &) {
					rejected = true;
				}
				check(rejected && !decoder.done());
				drain(decoder);
			}
			check(account->used() == 0);
		}
		{
			auto account = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
			d::Limits limits;
			limits.storage_account = account;
			t::ResponseDecoder decoder(cursor(), {}, d::NamespaceSelection(names), true, limits);
			auto wire = response(d::UpdateKind::Snapshot, true, true, 10000);
			consume(decoder, wire, 127);
			auto result = decoder.take();
			check(result.snapshot->records().size() == 10000);
			drain(decoder);
		}
		std::cout << "incremental delivery response passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
