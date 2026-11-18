#include "detent_local/layout.hh"
#include "detent_local/ownership.hh"
#include "detent_local/paged_layout.hh"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <sha256.h>
#include <source_location>
#include <stdexcept>
#include <string_view>

namespace l = detent::local;
namespace {
void check(bool value, std::source_location at = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("segment ownership invariant failed at line " + std::to_string(at.line()));
}
template <class Action> void rejected(Action action, std::source_location at = std::source_location::current()) {
	bool failed = false;
	try {
		action();
	} catch (const std::invalid_argument &) {
		failed = true;
	}
	check(failed, at);
}
template <std::size_t N> std::array<char, N> unhex(std::string_view hex) {
	check(hex.size() == N * 2);
	std::array<char, N> bytes{};
	for (std::size_t at = 0; at < N; ++at) {
		auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
		bytes[at] = static_cast<char>(digit(hex[at * 2]) * 16 + digit(hex[at * 2 + 1]));
	}
	return bytes;
}
template <std::size_t N> void reseal(std::array<char, N> &bytes) {
	SHA256 hash;
	hash.add(bytes.data(), N - 32);
	unsigned char digest[32];
	hash.getHash(digest);
	std::memcpy(bytes.data() + N - 32, digest, sizeof digest);
}
} // namespace
int main() {
	try {
		// An independent Python struct.pack/hashlib vector, not a codec-produced expectation.
		l::SegmentId id{};
		std::array<char, 32> checksum{};
		for (unsigned at = 0; at < 16; ++at)
			id[at] = static_cast<char>(at + 17);
		for (unsigned at = 0; at < 32; ++at)
			checksum[at] = static_cast<char>(at);
		const auto segment_bytes =
			unhex<128>("445453474f574e3200000001000000801112131415161718191a1b1c1d1e1f20"
					   "0000000000001000000102030405060708090a0b0c0d0e0f1011121314151617"
					   "18191a1b1c1d1e1f000000000000000000000000000000000000000000000000"
					   "5294252892feaa63cadffc53d4dea9b30fd676250293606e218fd26c4e74c87d");
		l::SegmentOwnership segment{id, 4096, checksum};
		check(l::decode_segment_ownership(segment_bytes) == segment &&
			  l::encode_segment_ownership(segment) == segment_bytes);
		for (std::size_t at = 0; at < segment_bytes.size(); ++at) {
			auto broken = segment_bytes;
			broken[at] ^= 1;
			rejected([&] { l::decode_segment_ownership(broken); });
		}
		for (std::size_t length = 0; length < segment_bytes.size(); ++length)
			rejected([&] { l::decode_segment_ownership(std::span(segment_bytes).first(length)); });
		for (auto offset : {0u, 8u, 12u, 72u, 88u}) {
			auto broken = segment_bytes;
			broken[offset] ^= 1;
			reseal(broken);
			rejected([&] { l::decode_segment_ownership(broken); });
		}
		for (auto length : {0u, 63u, 1048577u}) {
			auto broken = segment_bytes;
			l::write_integer(broken, 32, length, 8);
			reseal(broken);
			rejected([&] { l::decode_segment_ownership(broken); });
			segment.length = length;
			rejected([&] { l::encode_segment_ownership(segment); });
		}
		segment.length = 64;
		check(l::decode_segment_ownership(l::encode_segment_ownership(segment)) == segment);
		segment.length = 1048576;
		check(l::decode_segment_ownership(l::encode_segment_ownership(segment)) == segment);
		std::cout
			<< "the segment owner record's independent vector, corruption and exact bounds passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
