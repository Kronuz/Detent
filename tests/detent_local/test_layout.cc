#include "detent_local/layout.hh"
#include <iostream>
#include <limits>
namespace l = detent::local;
void require(bool value) {
	if (!value)
		throw std::runtime_error("local layout invariant failed");
}
int main() {
	try {
		l::Header header;
		header.layout = l::calculate_layout(1, 500000, 100000, 16000000);
		header.cursor.cluster.fill('c');
		header.cursor.group.fill('g');
		header.cursor.restore_epoch.fill('e');
		header.cursor.scope.fill('s');
		header.cursor.revision = std::numeric_limits<std::uint64_t>::max();
		auto encoded = l::encode_header(header);
		auto decoded = l::decode_header(encoded);
		require(decoded.cursor == header.cursor && decoded.layout.length == header.layout.length &&
				decoded.layout.record_offset == 216);
		require(header.layout.length > 4u * 1024 * 1024);
		for (std::size_t offset : {0u, 8u, 12u, 16u, 40u, 48u, 56u, 64u, 168u, 172u, 176u, 184u}) {
			auto broken = encoded;
			broken[offset] ^= 0x7f;
			bool rejected = false;
			try {
				l::decode_header(broken);
			} catch (const std::exception &) {
				rejected = true;
			}
			require(rejected);
		}
		for (auto size : {0u, 1u, 191u}) {
			bool rejected = false;
			try {
				l::decode_header(std::span(encoded).first(size));
			} catch (const std::exception &) {
				rejected = true;
			}
			require(rejected);
		}
		bool rejected = false;
		try {
			l::calculate_layout(0, 0, std::numeric_limits<std::uint64_t>::max(), 0,
								std::numeric_limits<std::uint64_t>::max());
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		require(l::calculate_layout(0, 0, 0, 0, 192).length == 192);
		rejected = false;
		try {
			l::calculate_layout(0, 0, 0, 1, 192);
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		std::cout << "portable dynamic local layout passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
