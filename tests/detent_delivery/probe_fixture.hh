#pragma once
#include "detent_distribution/framing.hh"
#include "detent_distribution/messagepack.hh"
#include <algorithm>
#include <array>
#include <charconv>
#include <sha256.h>
#include <source_location>
#include <stdexcept>

namespace probe_fixture {
namespace d = detent::distribution;
namespace m = d::messagepack;
inline void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("probe invariant at " + std::to_string(where.line()));
}
inline d::Cursor cursor(std::uint64_t revision = 5) {
	d::Cursor value;
	value.cluster.fill('c');
	value.group.fill('g');
	value.restore_epoch.fill('e');
	value.scope.fill('s');
	value.revision = revision;
	return value;
}
inline d::Identity session() {
	d::Identity value;
	value.fill('a');
	return value;
}
inline std::string session_hex() { return "61616161616161616161616161616161"; }
inline void put_cursor(std::string &out, d::Cursor value) {
	m::array(out, 5);
	for (const auto *part : {&value.cluster, &value.group, &value.restore_epoch})
		m::string(out, {part->data(), part->size()}, true);
	m::string(out, {value.scope.data(), value.scope.size()}, true);
	m::unsigned_integer(out, value.revision);
}
inline std::string semantic(d::UpdateKind kind = d::UpdateKind::Snapshot, std::uint64_t revision = 5) {
	std::string out;
	const bool progress = kind == d::UpdateKind::Progress;
	m::array(out, progress ? 5 : 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, static_cast<unsigned>(kind));
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	if (kind != d::UpdateKind::Snapshot)
		put_cursor(out, cursor(4));
	put_cursor(out, cursor(revision));
	if (progress)
		return out;
	m::array(out, 4);
	for (const auto count : {1, 1, 0, 14})
		m::unsigned_integer(out, count);
	if (kind == d::UpdateKind::Delta) {
		m::array(out, 1);
		m::array(out, 3);
		m::unsigned_integer(out, revision);
	}
	m::array(out, 1);
	m::array(out, 2);
	m::string(out, "ns");
	m::unsigned_integer(out, revision);
	m::array(out, 1);
	m::array(out, 4);
	m::unsigned_integer(out, 0);
	m::string(out, "ns:k00000000");
	m::unsigned_integer(out, kind == d::UpdateKind::Snapshot ? revision : 0);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::signed_integer(out, 7);
	return out;
}
inline std::array<unsigned char, 32> digest(std::string_view bytes) {
	class SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<unsigned char, 32> value;
	hash.getHash(value.data());
	return value;
}
struct Wire {
	std::string bytes;
	std::size_t body_bytes;
};
inline Wire response(std::string_view payload, bool compression, bool chunked = false,
					 std::size_t width = d::fragment_bytes, char identity = 'u',
					 d::UpdateKind kind = d::UpdateKind::Snapshot, std::uint64_t window = 1) {
	d::Identity update;
	update.fill(identity);
	std::string body;
	for (std::size_t offset = 0; offset < payload.size();) {
		const auto n = std::min(width, payload.size() - offset);
		body += d::encode_fragment({kind, update, offset, payload.size(), offset + n == payload.size(),
								  std::string(payload.substr(offset, n))}, compression);
		offset += n;
	}
	std::string head = "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.detent.distribution\r\n"
		"X-Detent-Profile: 1\r\nX-Detent-Revision: 5\r\nX-Detent-Kind: " +
		std::to_string(static_cast<unsigned>(kind)) + "\r\nX-Detent-Coding: " +
		(compression ? "zstd" : "raw") + "\r\nX-Detent-Session-Version: 1\r\nX-Detent-Session: " +
		session_hex() + "\r\nX-Detent-Window: " + std::to_string(window) + "\r\n";
	if (!chunked)
		return {head + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body, body.size()};
	head += "Transfer-Encoding: chunked\r\n\r\n";
	for (std::size_t offset = 0; offset < body.size();) {
		const auto n = std::min<std::size_t>(137, body.size() - offset);
		char hex[32];
		const auto value = std::to_chars(hex, hex + sizeof(hex), n, 16);
		head.append(hex, value.ptr);
		head += "\r\n";
		head.append(body, offset, n);
		head += "\r\n";
		offset += n;
	}
	return {head + "0\r\n\r\n", body.size()};
}
inline std::string control(unsigned status, std::string body = {}) {
	return "HTTP/1.1 " + std::to_string(status) + " Control\r\nContent-Type: application/json\r\n"
		"Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
inline void replace(std::string &wire, std::string_view before, std::string_view after) {
	const auto at = wire.find(before);
	check(at != std::string::npos);
	wire.replace(at, before.size(), after);
}
} // namespace probe_fixture
