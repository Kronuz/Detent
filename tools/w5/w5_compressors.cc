// Compressors through the old and the new pins: valid streams (compressed bytes and decoded bytes), damaged
// streams (truncated, flipped, garbage, crafted lengths) and XXH32 digests. Detent decodes zstd and gzip in
// detent_http/http_client.cc and links zstd for its distribution frames; this exercises the same entry points.
#include "common.hh"

#include <compressor_deflate.h>
#include <compressor_lz4.h>
#include <compressor_zstd.h>

#include <cstring>
#include <random>
#include <stdexcept>
#include <typeinfo>
#include <vector>

namespace {

std::string text_sample(std::size_t n, unsigned seed) {
	std::mt19937 rng(seed);
	std::string s;
	while (s.size() < n) {
		s += (rng() % 3) ? std::string("the quick brown fox jumps over the lazy dog. ") : std::to_string(rng());
	}
	s.resize(n);
	return s;
}

std::string random_sample(std::size_t n, unsigned seed) {
	std::mt19937_64 rng(seed);
	std::string s(n, '\0');
	for (auto& c : s) c = static_cast<char>(rng());
	return s;
}

template <typename F>
std::string outcome(F&& decode) {
	try {
		std::string out = decode();
		return "ok " + std::to_string(out.size()) + " " + w5::hex(w5::fnv(out));
	} catch (const ZstdCorruptVolume&) {
		return "throw ZstdCorruptVolume";
	} catch (const ZstdException&) {
		return "throw ZstdException";
	} catch (const LZ4CorruptVolume&) {
		return "throw LZ4CorruptVolume";
	} catch (const LZ4Exception&) {
		return "throw LZ4Exception";
	} catch (const DeflateException&) {
		return "throw DeflateException";
	} catch (const std::bad_alloc&) {
		return "throw bad_alloc";
	} catch (const std::exception& e) {
		return std::string("throw std:") + typeid(e).name();
	} catch (...) {
		return "throw unknown";
	}
}

template <typename Codec>
std::string via_stream(std::string_view s, bool gzip = false) {
	std::string out;
	if constexpr (std::is_constructible_v<Codec, const char*, std::size_t, bool>) {
		Codec c(s.data(), s.size(), gzip);
		for (auto it = c.begin(); it; ++it) out.append(*it);
	} else {
		Codec c(s.data(), s.size());
		for (auto it = c.begin(); it; ++it) out.append(*it);
	}
	return out;
}

struct Codec {
	const char* name;
	std::function<std::string(std::string_view)> compress;
	std::function<std::string(std::string_view)> decompress;
};

std::vector<Codec> codecs() {
	return {
		{"zstd-oneshot", [](std::string_view s) { return compress_zstd(s); }, [](std::string_view s) { return decompress_zstd(s); }},
		{"zstd-stream", [](std::string_view s) { return via_stream<ZstdCompressData>(s); }, [](std::string_view s) { return via_stream<ZstdDecompressData>(s); }},
		{"lz4-oneshot", [](std::string_view s) { return compress_lz4(s); }, [](std::string_view s) { return decompress_lz4(s); }},
		{"lz4-stream", [](std::string_view s) { return via_stream<LZ4CompressData>(s); }, [](std::string_view s) { return via_stream<LZ4DecompressData>(s); }},
		{"gzip-stream", [](std::string_view s) { return via_stream<DeflateCompressData>(s, true); }, [](std::string_view s) { return via_stream<DeflateDecompressData>(s, true); }},
		{"deflate-stream", [](std::string_view s) { return via_stream<DeflateCompressData>(s, false); }, [](std::string_view s) { return via_stream<DeflateDecompressData>(s, false); }},
	};
}

std::string lz4_record(std::uint16_t cmp_bytes, std::size_t body) {
	std::string s(reinterpret_cast<const char*>(&cmp_bytes), sizeof(cmp_bytes));
	return s + text_sample(body, 7);
}

} // namespace

int main() {
	const auto all = codecs();
	// 1. Valid streams: the compressed bytes and the decoded bytes.
	for (const auto& codec : all) {
		for (std::size_t n : {0u, 1u, 13u, 1000u, 70000u, 300000u}) {
			for (int kind = 0; kind < 2; ++kind) {
				std::string input = kind ? random_sample(n, 5 + n) : text_sample(n, 3 + n);
				std::string id = std::string("valid ") + codec.name + (kind ? " random " : " text ") + std::to_string(n);
				std::string packed;
				std::string r = w5::isolated([&] {
					packed = codec.compress(input);
					auto back = outcome([&] { return codec.decompress(packed); });
					bool same = back == "ok " + std::to_string(input.size()) + " " + w5::hex(w5::fnv(input));
					return "packed " + std::to_string(packed.size()) + " " + w5::hex(w5::fnv(packed)) + " roundtrip " + (same ? "same" : "DIFFERENT " + back);
				});
				w5::line(id, r);
			}
		}
	}
	// 2. Damaged streams: truncations, byte and bit flips, trailing and leading garbage, random bytes.
	for (const auto& codec : all) {
		for (std::size_t n : {1000u, 70000u}) {
			const std::string input = text_sample(n, 11);
			const std::string good = codec.compress(input);
			const std::size_t stride = std::max<std::size_t>(1, good.size() / 150);
			for (std::size_t cut = 0; cut < good.size(); cut += stride) {
				w5::line(std::string("truncate ") + codec.name + " " + std::to_string(n) + " " + std::to_string(cut),
						 w5::isolated([&] { return outcome([&] { return codec.decompress(std::string_view(good).substr(0, cut)); }); }));
			}
			for (std::size_t at = 0; at < good.size(); at += stride) {
				for (int bit : {0, 3, 7}) {
					std::string bad = good;
					bad[at] = static_cast<char>(bad[at] ^ (1 << bit));
					w5::line(std::string("flip ") + codec.name + " " + std::to_string(n) + " " + std::to_string(at) + "." + std::to_string(bit),
							 w5::isolated([&] { return outcome([&] { return codec.decompress(bad); }); }));
				}
			}
			std::string tail = good + random_sample(64, 17);
			w5::line(std::string("trailing ") + codec.name + " " + std::to_string(n), w5::isolated([&] { return outcome([&] { return codec.decompress(tail); }); }));
			std::string twice = good + good;
			w5::line(std::string("concatenated ") + codec.name + " " + std::to_string(n), w5::isolated([&] { return outcome([&] { return codec.decompress(twice); }); }));
		}
		for (std::size_t n = 0; n < 64; ++n) {
			std::string junk = random_sample(n, 100 + n);
			w5::line(std::string("garbage ") + codec.name + " " + std::to_string(n), w5::isolated([&] { return outcome([&] { return codec.decompress(junk); }); }));
		}
		w5::line(std::string("empty ") + codec.name, w5::isolated([&] { return outcome([&] { return codec.decompress(std::string_view()); }); }));
	}
	// 3. Crafted: LZ4 record lengths, a zstd frame that claims 1 TiB, a zlib preset-dictionary header.
	constexpr std::size_t bound = LZ4_COMPRESSBOUND(LZ4_BLOCK_SIZE);
	std::vector<std::pair<std::string, std::string>> crafted;
	for (std::size_t n : {bound + 1, bound + 2, std::size_t(4096), std::size_t(30000), std::size_t(65535), bound})
		crafted.push_back({"lz4 record " + std::to_string(n), lz4_record(static_cast<std::uint16_t>(n), n)});
	crafted.push_back({"lz4 record zero length", lz4_record(0, 100)});
	crafted.push_back({"lz4 record 65535 of 100", lz4_record(65535, 100)});
	crafted.push_back({"lz4 one byte", std::string("\x01", 1)});
	const std::string good_lz4 = compress_lz4(text_sample(100000, 3));
	crafted.push_back({"lz4 good then 65535", good_lz4 + lz4_record(65535, 65535)});
	crafted.push_back({"lz4 good then bound+1", good_lz4 + lz4_record(static_cast<std::uint16_t>(bound + 1), bound + 1)});
	std::string huge("\x28\xb5\x2f\xfd\xe0", 5);
	huge += std::string("\x00\x00\x00\x00\x00\x01\x00\x00", 8);
	crafted.push_back({"zstd claims 1TiB", huge});
	std::string unbounded("\x28\xb5\x2f\xfd\xe0", 5);
	unbounded += std::string(8, '\xff');
	crafted.push_back({"zstd claims unbounded", unbounded});
	crafted.push_back({"zstd magic only", std::string("\x28\xb5\x2f", 3)});
	crafted.push_back({"zlib preset dictionary", std::string("\x78\xbb\x00\x00\x00\x01\x03\x00\x00\x00\x00\x01", 12)});
	crafted.push_back({"gzip header only", std::string("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03", 10)});
	for (const auto& [name, bytes] : crafted) {
		for (const auto& codec : all) {
			if ((name.rfind("lz4", 0) == 0) != (std::string_view(codec.name).rfind("lz4", 0) == 0)) continue;
			if ((name.rfind("zstd", 0) == 0) != (std::string_view(codec.name).rfind("zstd", 0) == 0)) continue;
			if ((name.rfind("zlib", 0) == 0 || name.rfind("gzip", 0) == 0) && std::string_view(codec.name).find("deflate") == std::string_view::npos && std::string_view(codec.name).find("gzip") == std::string_view::npos) continue;
			w5::line("crafted " + name + " via " + codec.name, w5::isolated([&] { return outcome([&] { return codec.decompress(bytes); }); }));
		}
	}
	// 4. zstd with a size bound, as Detent's callers could pass one.
	for (std::size_t limit : {std::size_t(0), std::size_t(10), std::size_t(1000), std::size_t(1) << 20}) {
		const std::string good = compress_zstd(text_sample(5000, 21));
		w5::line("zstd bound " + std::to_string(limit), w5::isolated([&] { return outcome([&] { return decompress_zstd(good, limit); }); }));
		w5::line("zstd bound huge " + std::to_string(limit), w5::isolated([&] { return outcome([&] { return decompress_zstd(huge, limit); }); }));
	}
	// 5. XXH32: the one-shot digest of several sizes and seeds, in one update and in odd pieces.
	for (std::size_t n : {0u, 1u, 3u, 4u, 15u, 16u, 17u, 255u, 256u, 1000u, 65536u, 100001u}) {
		for (std::uint32_t seed : {0u, 1u, 0x9e3779b1u}) {
			std::string data = random_sample(n, 31 + n);
			compressors::XXH32_state_t state;
			compressors::XXH32_reset(&state, seed);
			compressors::XXH32_update(&state, data.data(), data.size());
			auto whole = compressors::XXH32_digest(&state);
			compressors::XXH32_reset(&state, seed);
			for (std::size_t at = 0; at < data.size(); at += 7)
				compressors::XXH32_update(&state, data.data() + at, std::min<std::size_t>(7, data.size() - at));
			auto pieces = compressors::XXH32_digest(&state);
			char b[64];
			std::snprintf(b, sizeof b, "whole %08x pieces %08x", whole, pieces);
			w5::line("xxh32 " + std::to_string(n) + " seed " + std::to_string(seed), b);
		}
	}
	return 0;
}
