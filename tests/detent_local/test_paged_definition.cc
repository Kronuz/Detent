#include "detent_local/layout.hh"
#include "detent_local/paged_definition.hh"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace p = detent::local::paged;
namespace l = detent::local;
namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("paged definition invariant at line " + std::to_string(line));
}
#define CHECK(value) check(value, __LINE__)
std::vector<char> encode(const detent::KnobDef &definition, std::uint64_t revision = 7) {
	std::vector<char> body(p::definition_size(definition));
	p::encode_definition(body, definition, revision);
	return body;
}
// The definition a body encodes, read back through the validating decoder.
detent::KnobDef decode(std::span<const char> body, std::uint64_t revision = 7) {
	const auto header = p::validate_definition(body, revision);
	detent::KnobDef result{header.default_value, {}, header.rollout, header.rollout_value};
	std::size_t offset = p::header_size(header);
	for (std::uint32_t i = 0; i != header.overrides; ++i) {
		const auto row = p::read_override(body, offset);
		result.overrides.emplace(std::string(row.key), row.value);
	}
	return result;
}
bool refused(std::span<const char> body, std::uint64_t revision = 7) {
	try {
		p::validate_definition(body, revision);
	} catch (const std::invalid_argument &) {
		return true;
	}
	return false;
}
bool refused_shape(const detent::KnobDef &definition) {
	try {
		(void)p::definition_size(definition);
	} catch (const std::invalid_argument &) {
		return true;
	}
	return false;
}
} // namespace
int main() {
	// Round trips: plain integers, a bool ramp, an integer ramp with forced and percentage groups.
	detent::KnobDef ramp{25, {}, 10, 50};
	ramp.overrides.emplace("beta", std::int64_t(7));
	ramp.overrides.emplace("canary", detent::GroupRollout{50});
	ramp.overrides.emplace("zz", std::int64_t(INT64_MIN));
	detent::KnobDef group_only{-3, {}, std::nullopt, INT64_MAX};
	group_only.overrides.emplace("g", detent::GroupRollout{100});
	for (const auto &definition : {detent::KnobDef{INT64_MIN}, detent::KnobDef{INT64_MAX},
								   detent::KnobDef{0, {}, 0, 1}, detent::KnobDef{1, {}, 100, 1}, ramp, group_only}) {
		const auto body = encode(definition);
		CHECK(decode(body) == definition);
		CHECK(body.size() == p::definition_size(definition));
	}
	// Smallest encodings: a one-byte default and count, a one-byte rollout value, a one-byte forced row value.
	CHECK(encode(detent::KnobDef{17}).size() == 12 && encode(detent::KnobDef{0, {}, 50, 1}).size() == 13);
	CHECK(encode(detent::KnobDef{INT64_MIN}).size() == 21 && encode(detent::KnobDef{-64}).size() == 12 &&
		  encode(detent::KnobDef{64}).size() == 13);
	detent::KnobDef forced{0};
	forced.overrides.emplace("g", std::int64_t(-1));
	CHECK(encode(forced).size() == 12 + 2 + 1 + 1);

	// The encoder refuses invalid shapes.
	CHECK(refused_shape(detent::KnobDef{0, {}, 50, std::nullopt}));    // percentage without a value
	CHECK(refused_shape(detent::KnobDef{0, {}, std::nullopt, 1}));     // value without a percentage
	CHECK(refused_shape(detent::KnobDef{0, {}, 101, 1}));              // percentage over 100
	detent::KnobDef group_without_value{0};
	group_without_value.overrides.emplace("g", detent::GroupRollout{5});
	CHECK(refused_shape(group_without_value));

	// The decoder refuses corrupt bodies.
	const auto good = encode(ramp);
	CHECK(!refused(good) && refused(good, 6));                        // revision above the namespace's
	auto bad = good;
	l::write_integer(bad, 0, 0, 8);
	CHECK(refused(bad));                                              // revision zero
	bad = good;
	bad[9] = 0;
	CHECK(refused(bad));                                              // rollout without a value
	bad = good;
	bad[9] = 2;
	CHECK(refused(bad));                                              // unknown flag
	bad = good;
	bad[8] = char(101);
	CHECK(refused(bad));                                              // percentage over 100
	auto lone = encode(detent::KnobDef{0, {}, 50, 1});
	lone[8] = char(255);
	CHECK(refused(lone));                                             // value with no percentage left
	// A forced row {"g", 0}: length 1, kind 0, value varint at byte 14, key at byte 15.
	detent::KnobDef zero{0};
	zero.overrides.emplace("g", std::int64_t(0));
	auto overlong = encode(zero);
	CHECK(!refused(overlong) && overlong.size() == 16 && overlong[14] == 0 && overlong[15] == 'g');
	overlong[14] = char(0x80);
	overlong.insert(overlong.begin() + 15, char(0x00));
	CHECK(refused(overlong));                                         // non-canonical varint in a row
	overlong = encode(zero);
	overlong.erase(overlong.begin() + 14);
	overlong.insert(overlong.begin() + 14, 9, char(0xff));
	overlong.insert(overlong.begin() + 14 + 9, char(0x7f));
	CHECK(refused(overlong));                                         // row varint past 64 bits
	bad = good;
	bad.push_back(0);
	CHECK(refused(bad));                                              // trailing bytes
	bad = good;
	bad.pop_back();
	CHECK(refused(bad));                                              // truncated
	// ramp: header 13 bytes (default 25, count 3, value 50 one byte each), then "beta" forced 7
	// (2 + 1 + 4 bytes), then "canary" at 50% (2 + 1 + 6 bytes).
	const std::size_t first = 13, second = first + 7;
	CHECK(p::header_size(p::validate_definition(good, 7)) == first);
	bad = good;
	bad[second + 1] = 2;
	CHECK(refused(bad));                                              // unknown override kind
	bad = good;
	bad[second + 2] = char(101);
	CHECK(refused(bad));                                              // group percentage over 100
	bad = good;
	bad[first] = 0;
	CHECK(refused(bad));                                              // empty key
	bad = good;
	bad[11] = 4;
	CHECK(refused(bad));                                              // count beyond the rows
	detent::KnobDef pair{0};
	pair.overrides.emplace("a", std::int64_t(1));
	pair.overrides.emplace("b", std::int64_t(2));
	bad = encode(pair);
	std::swap(bad[12 + 3], bad[12 + 4 + 3]);
	CHECK(refused(bad));                                              // keys out of order
	bad = encode(pair);
	bad[12 + 4 + 3] = char(0xff);
	CHECK(refused(bad));                                              // key not UTF-8
	std::cout << "paged definitions round-trip and refuse invalid shapes\n";
	return 0;
}
