// Reads one upstream (a voter or a relay) through HttpUpstream and prints, as JSON, the digest of
// every answer test_relay_service.py compares: the layouts, each file, and the W6 body at every
// cursor of every namespace (none, another incarnation, and R = 0 .. head + 1), raw, and zstd at
// none, another incarnation, R = 0 and the last three.
//
//   relay_probe CA PORT TOKEN CLUSTER-HEX NAME...
#include "detent_distribution/session_frames.hh"
#include "detent_relay/http_upstream.hh"
#include "sha256.h"
#include <cstdio>
#include <iostream>
#include <map>
#include <reactor_tls.h>

namespace d = detent::distribution;
namespace r = detent::relay;
namespace {
std::string sha(std::string_view bytes) {
	class SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	return hash.getHash();
}
d::Identity identity(std::string_view text) {
	d::Identity out{};
	if (text.size() != 2 * out.size())
		throw std::invalid_argument("cluster hex");
	for (std::size_t i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::stoi(std::string(text.substr(2 * i, 2)), nullptr, 16));
	return out;
}
// The head each frame reaches, by name.
struct Heads final : d::FrameSink {
	std::map<std::string, d::NamespaceCursor> heads;
	void frame(std::string_view name, d::FrameKind, std::uint64_t, const d::NamespaceCursor &to) override {
		heads[std::string(name)] = to;
	}
	void piece(d::PieceKind, std::uint64_t, std::uint64_t) override {}
	void record(std::string_view, std::uint64_t, const detent::KnobDef *) override {}
};
std::map<std::string, d::NamespaceCursor> heads(std::string_view body) {
	Heads sink;
	d::SessionFramesDecoder decoder(sink);
	std::span<const char> input(body.data(), body.size());
	while (!input.empty()) {
		std::size_t budget = input.size();
		decoder.feed(input, budget);
	}
	decoder.finish();
	return sink.heads;
}
const char *kind(r::MirrorFile::Kind k) {
	return k == r::MirrorFile::Kind::Base ? "base" : k == r::MirrorFile::Kind::Block ? "block" : "section";
}
} // namespace

int main(int argc, char **argv) {
	try {
		if (argc < 6)
			throw std::invalid_argument("usage: relay_probe CA PORT TOKEN CLUSTER-HEX NAME...");
		const auto cluster = identity(argv[4]);
		const std::vector<std::string> names(argv + 5, argv + argc);
		auto upstream = [&](bool compression) {
			r::HttpUpstreamOptions options;
			options.endpoints.emplace_back(asio::ip::address_v4::loopback(), std::stoi(argv[2]));
			options.hostname = "localhost";
			options.token = argv[3];
			options.compression = compression;
			options.tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
			reactor::tls::configure(*options.tls, {"", "", argv[1]}, reactor::tls::Role::Client);
			return std::make_unique<r::HttpUpstream>(std::move(options));
		};
		auto raw = upstream(false), zstd = upstream(true);
		std::string out = "{";
		const auto layouts = raw->layouts(names);
		out += "\"layouts\":\"" + sha(r::encode_mirror_table({0, layouts})) + "\",\"files\":[";
		bool first = true;
		for (const auto &layout : layouts)
			for (const auto &file : layout.files) {
				const auto bytes = raw->fetch(layout.name, file);
				out += std::string(first ? "" : ",") + "{\"namespace\":\"" + layout.name + "\",\"name\":\"" +
					   file.name + "\",\"kind\":\"" + kind(file.kind) + "\",\"sha\":\"" + sha(bytes) +
					   "\",\"bytes\":" + std::to_string(bytes.size()) + "}";
				first = false;
			}
		out += "],\"cuts\":{";
		first = true;
		for (const auto &layout : layouts) {
			out += std::string(first ? "" : ",") + "\"" + layout.name + "\":" + std::to_string(layout.cut);
			first = false;
		}
		std::vector<r::Position> everything;
		for (const auto &name : names)
			everything.push_back({name, std::nullopt});
		const auto reached = heads(raw->frames(everything));
		out += "},\"heads\":{";
		first = true;
		for (const auto &[name, cursor] : reached) {
			out += std::string(first ? "" : ",") + "\"" + name + "\":" + std::to_string(cursor.position);
			first = false;
		}
		out += "},\"frames\":{";
		first = true;
		// Raw at every cursor; zstd, the same frames coded again, at the cursors of each answer kind.
		auto answer = [&](const std::string &label, std::span<const r::Position> positions, bool coded = true) {
			for (auto *from : {raw.get(), zstd.get()}) {
				if (from == zstd.get() && !coded)
					break;
				out += std::string(first ? "" : ",") + "\"" + (from == raw.get() ? "raw " : "zstd ") + label +
					   "\":\"" + sha(from->frames(positions)) + "\"";
				first = false;
			}
		};
		answer("*", everything);
		for (const auto &[name, head] : reached) {
			r::Position position{name, std::nullopt};
			answer(name + " none", std::span(&position, 1));
			auto other = head;
			other.incarnation[0] ^= 1;
			other.position = 0;
			position.cursor = other;
			answer(name + " other", std::span(&position, 1));
			for (std::uint64_t at = 0; at <= head.position + 1; ++at) {
				position.cursor = d::NamespaceCursor{cluster, head.incarnation, at};
				answer(name + " " + std::to_string(at), std::span(&position, 1),
					   at == 0 || at + 2 >= head.position);
			}
		}
		out += "}}";
		std::cout << out << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::fprintf(stderr, "relay_probe: %s\n", error.what());
		return 1;
	}
}
