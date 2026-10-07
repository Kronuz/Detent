// Compare JSON, a narrow MessagePack tuple schema, and optional Protobuf Lite.
// Usage: detent_distribution_codec_bench json|messagepack|protobuf KNOBS
// Five warm encode/decode/compression timings; semantic oracle outside timing.
// All decoders build the same Snapshot. RSS includes setup and retained heaps.
// Protobuf is benchmark-only and requires DETENT_BENCHMARK_PROTOBUF=ON.
#ifndef DETENT_CODEC_NO_PROTOBUF
#include "distribution_codec_candidate.pb.h"
#endif
#include "detent/json_schema.hh"
#include "detent/validation.hh"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <vector>
#include <zstd.h>

using namespace detent;
using Clock = std::chrono::steady_clock;
void require(bool okay) {
	if (!okay)
		throw std::runtime_error("codec comparison oracle failed");
}

struct Pack {
	std::string data;
	void byte(unsigned n) { data += static_cast<char>(n); }
	void big(std::uint64_t n, unsigned count) {
		for (unsigned i = count; i; --i)
			byte(n >> ((i - 1) * 8));
	}
	void array(std::size_t n) {
		if (n < 16)
			byte(0x90 + n);
		else if (n <= 65535) {
			byte(0xdc);
			big(n, 2);
		} else {
			byte(0xdd);
			big(n, 4);
		}
	}
	void text(std::string_view s) {
		if (s.size() < 32)
			byte(0xa0 + s.size());
		else if (s.size() <= 255) {
			byte(0xd9);
			big(s.size(), 1);
		} else if (s.size() <= 65535) {
			byte(0xda);
			big(s.size(), 2);
		} else {
			byte(0xdb);
			big(s.size(), 4);
		}
		data.append(s);
	}
	void integer(std::int64_t n) {
		if (n >= 0 && n < 128)
			byte(n);
		else if (n >= -32 && n < 0)
			byte(static_cast<unsigned char>(n));
		else if (n >= 0) {
			unsigned width = n <= 255 ? 1 : n <= 65535 ? 2 : n <= UINT32_MAX ? 4 : 8;
			byte(width == 1 ? 0xcc : width == 2 ? 0xcd : width == 4 ? 0xce : 0xcf);
			big(n, width);
		} else {
			unsigned width = n >= INT8_MIN ? 1 : n >= INT16_MIN ? 2 : n >= INT32_MIN ? 4 : 8;
			byte(width == 1 ? 0xd0 : width == 2 ? 0xd1 : width == 4 ? 0xd2 : 0xd3);
			big(std::bit_cast<std::uint64_t>(n), width);
		}
	}
	void boolean(bool b) { byte(b ? 0xc3 : 0xc2); }
};
std::string pack(const Snapshot &s) {
	Pack p;
	p.array(2);
	p.integer(s.version);
	p.array(s.knobs.size());
	for (const auto &[name, value] : s.knobs) {
		p.array(2);
		p.text(name);
		if (auto i = std::get_if<IntDef>(&value)) {
			p.array(2);
			p.integer(1);
			p.integer(i->default_value);
		} else {
			const auto &b = std::get<BoolDef>(value);
			p.array(4);
			p.integer(0);
			p.boolean(b.default_value);
			if (b.rollout_pct)
				p.integer(*b.rollout_pct);
			else
				p.byte(0xc0);
			p.array(b.overrides.size());
			for (const auto &[key, value] : b.overrides) {
				p.array(2);
				p.text(key);
				if (auto forced = std::get_if<bool>(&value))
					p.boolean(*forced);
				else
					p.integer(std::get<GroupRollout>(value).rollout_pct);
			}
		}
	}
	return std::move(p.data);
}
struct Unpack {
	std::string_view data;
	unsigned byte() {
		require(!data.empty());
		auto n = static_cast<unsigned char>(data.front());
		data.remove_prefix(1);
		return n;
	}
	std::uint64_t big(unsigned count) {
		std::uint64_t n = 0;
		for (unsigned i = 0; i < count; ++i)
			n = (n << 8) | byte();
		return n;
	}
	std::size_t array() {
		auto n = byte();
		if (n >= 0x90 && n <= 0x9f)
			return n - 0x90;
		require(n == 0xdc || n == 0xdd);
		return big(n == 0xdc ? 2 : 4);
	}
	std::string text() {
		auto n = byte();
		std::size_t count;
		if (n >= 0xa0 && n <= 0xbf)
			count = n - 0xa0;
		else {
			require(n == 0xd9 || n == 0xda || n == 0xdb);
			count = big(n == 0xd9 ? 1 : n == 0xda ? 2 : 4);
		}
		require(count <= data.size());
		std::string s(data.substr(0, count));
		data.remove_prefix(count);
		return s;
	}
	std::int64_t integer() {
		auto n = byte();
		if (n < 128)
			return n;
		if (n >= 0xe0)
			return static_cast<std::int8_t>(n);
		if (n >= 0xcc && n <= 0xcf) {
			auto value = big(1u << (n - 0xcc));
			require(value <= INT64_MAX);
			return value;
		}
		require(n >= 0xd0 && n <= 0xd3);
		auto width = 1u << (n - 0xd0);
		auto bits = big(width);
		if (width < 8 && (bits & (std::uint64_t{1} << (width * 8 - 1))))
			bits |= UINT64_MAX << (width * 8);
		return std::bit_cast<std::int64_t>(bits);
	}
	bool boolean() {
		auto n = byte();
		require(n == 0xc2 || n == 0xc3);
		return n == 0xc3;
	}
};
Snapshot unpack(std::string_view data, std::size_t count) {
	Unpack p{data};
	Snapshot s;
	require(p.array() == 2);
	s.version = p.integer();
	auto size = p.array();
	require(size == count);
	for (std::size_t i = 0; i < size; ++i) {
		require(p.array() == 2);
		auto name = p.text();
		require(is_valid_knob_name(name));
		auto fields = p.array();
		auto type = p.integer();
		if (type == 1) {
			require(fields == 2);
			require(s.knobs.emplace(std::move(name), IntDef{p.integer()}).second);
		} else {
			require(type == 0 && fields == 4);
			BoolDef b;
			b.default_value = p.boolean();
			require(!p.data.empty());
			if (static_cast<unsigned char>(p.data.front()) == 0xc0)
				p.byte();
			else {
				auto pct = p.integer();
				require(pct >= 0 && pct <= 100);
				b.rollout_pct = pct;
			}
			auto overrides = p.array();
			require(overrides <= 256);
			for (std::size_t j = 0; j < overrides; ++j) {
				require(p.array() == 2);
				auto key = p.text();
				require(!key.empty() && key.size() <= 128);
				require(!p.data.empty());
				auto tag = static_cast<unsigned char>(p.data.front());
				GroupOverride value;
				if (tag == 0xc2 || tag == 0xc3)
					value = p.boolean();
				else {
					auto pct = p.integer();
					require(pct >= 0 && pct <= 100);
					value = GroupRollout{static_cast<std::uint8_t>(pct)};
				}
				require(b.overrides.emplace(std::move(key), value).second);
			}
			require(s.knobs.emplace(std::move(name), std::move(b)).second);
		}
	}
	require(p.data.empty());
	return s;
}
#ifndef DETENT_CODEC_NO_PROTOBUF
std::string protobuf(const Snapshot &s) {
	detent_codec_trial::Snapshot p;
	p.set_revision(s.version);
	for (const auto &[name, value] : s.knobs) {
		auto *k = p.add_knobs();
		k->set_name(name);
		if (auto integer = std::get_if<IntDef>(&value))
			k->set_integer(integer->default_value);
		else {
			const auto &b = std::get<BoolDef>(value);
			auto *v = k->mutable_boolean();
			v->set_default_value(b.default_value);
			if (b.rollout_pct)
				v->set_rollout(*b.rollout_pct);
			for (const auto &[name, value] : b.overrides) {
				auto *o = v->add_overrides();
				o->set_name(name);
				if (auto forced = std::get_if<bool>(&value))
					o->set_forced(*forced);
				else
					o->set_rollout(std::get<GroupRollout>(value).rollout_pct);
			}
		}
	}
	std::string bytes;
	require(p.SerializeToString(&bytes));
	return bytes;
}
Snapshot unprotobuf(std::string_view bytes, std::size_t count) {
	detent_codec_trial::Snapshot p;
	require(bytes.size() < INT_MAX && p.ParseFromArray(bytes.data(), bytes.size()));
	require(static_cast<std::size_t>(p.knobs_size()) == count);
	Snapshot s;
	s.version = p.revision();
	for (const auto &k : p.knobs()) {
		require(is_valid_knob_name(k.name()));
		if (k.has_integer())
			require(s.knobs.emplace(k.name(), IntDef{k.integer()}).second);
		else {
			require(k.has_boolean());
			const auto &v = k.boolean();
			BoolDef b;
			b.default_value = v.default_value();
			if (v.has_rollout()) {
				require(v.rollout() <= 100);
				b.rollout_pct = v.rollout();
			}
			require(v.overrides_size() <= 256);
			for (const auto &o : v.overrides()) {
				require(!o.name().empty() && o.name().size() <= 128);
				GroupOverride value;
				if (o.has_forced())
					value = o.forced();
				else {
					require(o.has_rollout() && o.rollout() <= 100);
					value = GroupRollout{static_cast<std::uint8_t>(o.rollout())};
				}
				require(b.overrides.emplace(o.name(), value).second);
			}
			require(s.knobs.emplace(k.name(), std::move(b)).second);
		}
	}
	return s;
}
#else
std::string protobuf(const Snapshot &) { throw std::runtime_error("protobuf disabled"); }
Snapshot unprotobuf(std::string_view, std::size_t) { throw std::runtime_error("protobuf disabled"); }
#endif
std::string compress(std::string_view data) {
	std::string out(ZSTD_compressBound(data.size()), '\0');
	auto n = ZSTD_compress(out.data(), out.size(), data.data(), data.size(), 3);
	require(!ZSTD_isError(n));
	out.resize(n);
	return out;
}
void oracle(const Snapshot &a, const Snapshot &b) {
	require(a.version == b.version && a.knobs.size() == b.knobs.size());
	auto x = a.knobs.begin(), y = b.knobs.begin();
	while (x != a.knobs.end()) {
		require(x->first == y->first && x->second.index() == y->second.index());
		if (auto i = std::get_if<IntDef>(&x->second))
			require(i->default_value == std::get<IntDef>(y->second).default_value);
		else {
			const auto &l = std::get<BoolDef>(x->second), &r = std::get<BoolDef>(y->second);
			require(l.default_value == r.default_value && l.rollout_pct == r.rollout_pct &&
					l.overrides.size() == r.overrides.size());
			auto u = l.overrides.begin(), v = r.overrides.begin();
			while (u != l.overrides.end()) {
				require(u->first == v->first && u->second.index() == v->second.index());
				if (auto forced = std::get_if<bool>(&u->second))
					require(*forced == std::get<bool>(v->second));
				else
					require(std::get<GroupRollout>(u->second).rollout_pct ==
							std::get<GroupRollout>(v->second).rollout_pct);
				++u;
				++v;
			}
		}
		++x;
		++y;
	}
}
struct Timing {
	std::uint64_t wall_ns, cpu_ns;
};
template <class F> Timing median(F function) {
	std::vector<std::uint64_t> wall, cpu;
	for (unsigned i = 0; i < 5; ++i) {
		auto started_cpu = std::clock();
		auto begin = Clock::now();
		function();
		wall.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
		cpu.push_back((std::clock() - started_cpu) * 1000000000ull / CLOCKS_PER_SEC);
	}
	std::sort(wall.begin(), wall.end());
	std::sort(cpu.begin(), cpu.end());
	return {wall[2], cpu[2]};
}
int main(int argc, char **argv) {
	require(argc == 3);
	auto count = std::stoull(argv[2]);
	require(count && count <= 500000);
	std::string mode = argv[1];
	Snapshot source;
	source.version = 1;
	for (std::size_t i = 0; i < count; ++i) {
		auto name = "namespace" + std::to_string(i % 100) + ":service/feature_" + std::to_string(i);
		if (i % 2)
			source.knobs[name] = IntDef{static_cast<std::int64_t>(i)};
		else {
			BoolDef b;
			b.rollout_pct = 25;
			b.overrides["internal_employees"] = true;
			b.overrides["production"] = GroupRollout{5};
			source.knobs[name] = std::move(b);
		}
	}
	std::string bytes;
	Snapshot decoded;
	auto encode = [&] {
		if (mode == "protobuf")
			bytes = protobuf(source);
		else if (mode == "messagepack")
			bytes = pack(source);
		else {
			require(mode == "json");
			bytes = serialize_snapshot(source);
		}
	};
	auto decode = [&] {
		if (mode == "protobuf")
			decoded = unprotobuf(bytes, count);
		else if (mode == "messagepack")
			decoded = unpack(bytes, count);
		else {
			auto parsed = parse_source_snapshot(bytes, "comparison", count, 256u * 1024 * 1024);
			require(bool(parsed));
			decoded = std::move(parsed.value());
		}
	};
	auto encode_ns = median(encode);
	auto decode_ns = median(decode);
	oracle(source, decoded);
	std::string compressed;
	auto compress_ns = median([&] { compressed = compress(bytes); });
	rusage usage{};
	getrusage(RUSAGE_SELF, &usage);
	std::cout << "{\"codec\":\"" << mode << "\",\"knobs\":" << count << ",\"raw_bytes\":" << bytes.size()
			  << ",\"zstd3_bytes\":" << compressed.size() << ",\"encode_wall_ns\":" << encode_ns.wall_ns
			  << ",\"decode_wall_ns\":" << decode_ns.wall_ns
			  << ",\"compress_wall_ns\":" << compress_ns.wall_ns << ",\"encode_cpu_ns\":" << encode_ns.cpu_ns
			  << ",\"decode_cpu_ns\":" << decode_ns.cpu_ns << ",\"compress_cpu_ns\":" << compress_ns.cpu_ns
			  << ",\"max_rss_platform_units\":" << usage.ru_maxrss
			  << ",\"user_cpu_us\":" << usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec
			  << ",\"system_cpu_us\":" << usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec << "}\n";
}
