#include "reader_metadata.hh"
#include <algorithm>
#include <sha256.h>
#include <source_location>
#include <stdexcept>
namespace d = detent::local::detail;
namespace l = detent::local;
namespace {
void check(bool value, std::source_location at = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("reader metadata invariant at " + std::to_string(at.line()));
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
void reseal(std::array<char, d::reader_metadata_bytes> &bytes) {
	SHA256 hash;
	hash.add(bytes.data(), 160);
	std::array<unsigned char, 32> digest;
	hash.getHash(digest.data());
	std::copy(digest.begin(), digest.end(), bytes.begin() + 160);
}
} // namespace
int main() {
	d::ReaderMetadata value{};
	value.storage.fill(0x11);
	value.generation.fill(0x22);
	value.root_checksum.fill(0x33);
	value.counts = {2, 3, 3, 41, 2};
	value.physical_bytes = 8192;
	const auto bytes = d::encode_reader_metadata(value);
	// Independent struct.pack/hashlib vector.
	constexpr std::string_view hex = "445452454144303100000001000000c011111111111111111111111111111111"
	                                 "2222222222222222222222222222222233333333333333333333333333333333"
	                                 "3333333333333333333333333333333300000002000000030000000000000003"
	                                 "0000000000000029000000000000200000000002000000010000000000000000"
	                                 "0000000000000000000000000000000000000000000000000000000000000000"
	                                 "541249b056035be20e3454d8ca026b13f3a927e6383f257a75cc3e09ab221910";
	check(hex.size() == bytes.size() * 2);
	auto digit = [](char byte) { return byte <= '9' ? byte - '0' : byte - 'a' + 10; };
	for (std::size_t i = 0; i != bytes.size(); ++i)
		check(static_cast<unsigned char>(bytes[i]) == digit(hex[i * 2]) * 16 + digit(hex[i * 2 + 1]));
	check(d::encode_reader_metadata(d::decode_reader_metadata(bytes)) == bytes);
	for (std::size_t length = 0; length <= bytes.size(); ++length)
		d::validate_reader_metadata_prefix(std::span(bytes).first(length), value);
	for (auto offset : {116u, 119u, 120u, 159u}) {
		auto corrupt = bytes;
		corrupt[offset] ^= 2;
		rejected([&] { d::validate_reader_metadata_prefix(std::span(corrupt).first(offset + 1), value); });
	}
	{
		auto corrupt = bytes;
		l::write_integer(corrupt, 80, UINT32_MAX, 4);
		rejected([&] { d::validate_reader_metadata_prefix(std::span(corrupt).first(88), value); });
		corrupt = bytes;
		l::write_integer(corrupt, 112, 0, 4);
		rejected([&] { d::validate_reader_metadata_prefix(std::span(corrupt).first(116), value); });
	}
	for (std::size_t i = 0; i != bytes.size(); ++i) {
		auto corrupt = bytes;
		corrupt[i] ^= 1;
		rejected([&] { d::decode_reader_metadata(corrupt); });
	}
	rejected([&] { d::decode_reader_metadata(std::span(bytes).first(bytes.size() - 1)); });
	std::array<char, d::reader_metadata_bytes + 1> long_record{};
	std::copy(bytes.begin(), bytes.end(), long_record.begin());
	rejected([&] { d::decode_reader_metadata(long_record); });
	for (auto offset : {8u, 12u, 116u, 120u, 159u}) {
		auto corrupt = bytes;
		corrupt[offset] ^= 1;
		reseal(corrupt);
		rejected([&] { d::decode_reader_metadata(corrupt); });
	}
	for (unsigned kind = 0; kind != 7; ++kind) {
		auto invalid = value;
		switch (kind) {
		case 0:
			invalid.storage.fill(0);
			break;
		case 1:
			invalid.generation.fill(0);
			break;
		case 2:
			invalid.root_checksum.fill(0);
			break;
		case 3:
			invalid.counts.bools = UINT32_MAX;
			break;
		case 4:
			invalid.counts.maximum_overrides = 257;
			break;
		case 5:
			invalid.counts.overrides = 5;
			break;
		case 6:
			invalid.physical_bytes = l::paged::root_bytes - 1;
			break;
		}
		rejected([&] { d::encode_reader_metadata(invalid); });
	}
	value.counts = {};
	value.physical_bytes = l::paged::root_bytes;
	check(d::encode_reader_metadata(d::decode_reader_metadata(d::encode_reader_metadata(value))) ==
	      d::encode_reader_metadata(value));
}
