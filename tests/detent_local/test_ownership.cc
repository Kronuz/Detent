#include "detent_local/layout.hh"
#include "detent_local/ownership.hh"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <sha256.h>
#include <stdexcept>
#include <string_view>

namespace l = detent::local;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("storage ownership invariant failed");
}
template <class Action> void rejected(Action action) {
	bool failed = false;
	try {
		action();
	} catch (const std::invalid_argument &) {
		failed = true;
	}
	check(failed);
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
		// Independent Python struct.pack/hashlib vectors, not codec-produced expectations.
		auto storage = unhex<64>("445453544f52453100000001000000400102030405060708090a0b0c0d0e0f10"
								 "e47b4488cddef56ea4acb8a08f4b1fae95be4284fb76f16840fad71f58bd7a30");
		auto lease = unhex<128>("44544c454153453100000001000000800102030405060708090a0b0c0d0e0f10"
								"1112131415161718191a1b1c1d1e1f2000000000000000c00000000100000001"
								"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
								"09b83606e0e79be8f99e4e98adef24b80cc6871cff8627e9b843b306e6f9a7ff");
		l::StorageIdentity identity;
		l::GenerationOwnership ownership;
		for (unsigned at = 0; at < 16; ++at) {
			identity[at] = ownership.storage[at] = static_cast<char>(at + 1);
			ownership.generation[at] = static_cast<char>(at + 17);
		}
		for (unsigned at = 0; at < 32; ++at)
			ownership.image_checksum[at] = static_cast<char>(at);
		ownership.image_length = 192;
		check(l::decode_storage_identity(storage) == identity &&
			  l::encode_storage_identity(identity) == storage);
		check(l::decode_generation_ownership(lease) == ownership &&
			  l::encode_generation_ownership(ownership) == lease);
		const auto segment_bytes =
			unhex<128>("445453474f574e3100000001000000800102030405060708090a0b0c0d0e0f10"
					   "1112131415161718191a1b1c1d1e1f2000000000000010000000000200000000"
					   "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
					   "e1d52b12ea9819ad7cf59c397985892dabab6c74782d86284130d005f59e2d93");
		l::SegmentOwnership segment{ownership.storage, ownership.generation, 4096, ownership.image_checksum};
		check(l::decode_segment_ownership(segment_bytes) == segment &&
			  l::encode_segment_ownership(segment) == segment_bytes);
		rejected([&] { l::decode_generation_ownership(segment_bytes); });
		rejected([&] { l::decode_segment_ownership(lease); });
		for (std::size_t at = 0; at < segment_bytes.size(); ++at) {
			auto broken = segment_bytes;
			broken[at] ^= 1;
			rejected([&] { l::decode_segment_ownership(broken); });
		}
		for (std::size_t length = 0; length < segment_bytes.size(); ++length)
			rejected([&] { l::decode_segment_ownership(std::span(segment_bytes).first(length)); });
		for (auto offset : {0u, 8u, 12u, 56u, 60u}) {
			auto broken = segment_bytes;
			broken[offset] ^= 1;
			reseal(broken);
			rejected([&] { l::decode_segment_ownership(broken); });
		}
		for (auto length : {0u, 79u, 1048577u}) {
			auto broken = segment_bytes;
			l::write_integer(broken, 48, length, 8);
			reseal(broken);
			rejected([&] { l::decode_segment_ownership(broken); });
			segment.length = length;
			rejected([&] { l::encode_segment_ownership(segment); });
		}
		segment.length = 1048576;
		check(l::decode_segment_ownership(l::encode_segment_ownership(segment)) == segment);
		for (std::size_t at = 0; at < storage.size(); ++at) {
			auto broken = storage;
			broken[at] ^= 1;
			rejected([&] { l::decode_storage_identity(broken); });
		}
		for (std::size_t at = 0; at < lease.size(); ++at) {
			auto broken = lease;
			broken[at] ^= 1;
			rejected([&] { l::decode_generation_ownership(broken); });
		}
		for (std::size_t length = 0; length < storage.size(); ++length)
			rejected([&] { l::decode_storage_identity(std::span(storage).first(length)); });
		for (std::size_t length = 0; length < lease.size(); ++length)
			rejected([&] { l::decode_generation_ownership(std::span(lease).first(length)); });
		std::array<char, 65> long_storage{};
		std::array<char, 129> long_lease{};
		rejected([&] { l::decode_storage_identity(long_storage); });
		rejected([&] { l::decode_generation_ownership(long_lease); });
		// Valid checksums cannot make unsupported versions or fields acceptable.
		for (auto offset : {0u, 8u, 12u}) {
			auto broken = storage;
			broken[offset] ^= 1;
			reseal(broken);
			rejected([&] { l::decode_storage_identity(broken); });
		}
		for (auto offset : {0u, 8u, 12u, 56u, 60u}) {
			auto broken = lease;
			broken[offset] ^= 1;
			reseal(broken);
			rejected([&] { l::decode_generation_ownership(broken); });
		}
		auto zero_storage = storage;
		std::fill_n(zero_storage.begin() + 16, 16, 0);
		reseal(zero_storage);
		rejected([&] { l::decode_storage_identity(zero_storage); });
		for (auto offset : {16u, 32u}) {
			auto broken = lease;
			std::fill_n(broken.begin() + offset, 16, 0);
			reseal(broken);
			rejected([&] { l::decode_generation_ownership(broken); });
		}
		for (auto length : {0u, 191u}) {
			auto broken = lease;
			l::write_integer(broken, 48, length, 8);
			reseal(broken);
			rejected([&] { l::decode_generation_ownership(broken); });
			ownership.image_length = length;
			rejected([&] { l::encode_generation_ownership(ownership); });
		}
		ownership.image_length = std::numeric_limits<std::uint64_t>::max();
		check(l::decode_generation_ownership(l::encode_generation_ownership(ownership)) == ownership);
		rejected([&] { l::encode_storage_identity({}); });
		ownership.storage = {};
		rejected([&] { l::encode_generation_ownership(ownership); });
		std::cout
			<< "independent ownership vectors, corruption, exact bounds and quota independence passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
