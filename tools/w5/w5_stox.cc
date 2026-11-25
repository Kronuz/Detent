// strict-stox through the old and the new pins. Detent parses `--knob name=value` integers with
// strict_stoll (detent/overrides.cc); this feeds the library and that parser the same corpus: short and long
// integers, decimals, exponents, subnormals, inputs of 63, 64 and more bytes, leading zeros, and garbage.
#include "common.hh"

#include <strict_stox.hh>

#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

std::string bits(double v) {
	std::uint64_t u;
	std::memcpy(&u, &v, sizeof u);
	return w5::hex(u);
}

std::string bits(float v) {
	std::uint32_t u;
	std::memcpy(&u, &v, sizeof u);
	char b[16];
	std::snprintf(b, sizeof b, "%08x", u);
	return b;
}

std::string integer(const std::string& s, int base) {
	std::string out;
	{
		int e = 0;
		std::size_t idx = 99999;
		auto v = strict_stoll(&e, s, &idx, base);
		out += "ll=" + std::to_string(v) + " e=" + std::to_string(e) + " idx=" + std::to_string(idx);
	}
	{
		int e = 0;
		auto v = strict_stoll(&e, s, nullptr, base);
		out += " noidx=" + std::to_string(v) + " e=" + std::to_string(e);
	}
	{
		int e = 0;
		std::size_t idx = 99999;
		auto v = strict_stoull(&e, s, &idx, base);
		out += " ull=" + std::to_string(v) + " e=" + std::to_string(e) + " idx=" + std::to_string(idx);
	}
	{
		int e = 0;
		auto v = strict_stoi(&e, s, nullptr, base);
		out += " i=" + std::to_string(v) + " e=" + std::to_string(e);
	}
	try {
		auto v = strict_stoll(s, nullptr, base);
		out += " throw-api=" + std::to_string(v);
	} catch (const std::invalid_argument&) {
		out += " throw-api=invalid_argument";
	} catch (const std::out_of_range&) {
		out += " throw-api=out_of_range";
	}
	return out;
}

std::string floating(const std::string& s) {
	std::string out;
	{
		int e = 0;
		std::size_t idx = 99999;
		auto v = strict_stod(&e, s, &idx);
		out += "d=" + bits(v) + " e=" + std::to_string(e) + " idx=" + std::to_string(idx);
	}
	{
		int e = 0;
		auto v = strict_stod(&e, s, nullptr);
		out += " noidx=" + bits(v) + " e=" + std::to_string(e);
	}
	{
		int e = 0;
		std::size_t idx = 99999;
		auto v = strict_stof(&e, s, &idx);
		out += " f=" + bits(v) + " e=" + std::to_string(e) + " idx=" + std::to_string(idx);
	}
	return out;
}

} // namespace

int main() {
	std::vector<std::string> ints = {"0", "-0", "+0", "1", "-1", "+1", "42", "007", "9223372036854775807", "9223372036854775808", "-9223372036854775808", "-9223372036854775809",
									 "18446744073709551615", "18446744073709551616", "2147483647", "2147483648", "-2147483648", "-2147483649", "4294967295", "4294967296",
									 "", " ", "+", "-", "--1", "+-1", "1 ", " 1", "1\n", "\t1", "1a", "a1", "0x10", "0X1f", "0b11", "1_000", "1,000", "1.5", "1e3", "٣", "１２", "\xc2\xa0" "1",
									 "ff", "FF", "-ff", "zz", "Zz", "0x", "0xg", "08", "0777", "00000000000000000000", "1" + std::string(18, '0'), "1" + std::string(19, '0')};
	// Lengths around the old 64-byte buffer: digits, leading zeros, trailing garbage.
	for (std::size_t n : {20u, 31u, 62u, 63u, 64u, 65u, 66u, 100u, 200u, 1000u}) {
		ints.push_back(std::string(n, '0') + "5");
		ints.push_back("-" + std::string(n, '0') + "5");
		ints.push_back(std::string(n, '1'));
		ints.push_back("5" + std::string(n, ' '));
		ints.push_back("5" + std::string(n, 'x'));
		ints.push_back(std::string(n, '0'));
		ints.push_back("+" + std::string(n, '0') + "9223372036854775807");
	}
	for (const auto& s : ints) {
		for (int base : {10, 16, 0, 2, 36}) {
			w5::line("int base " + std::to_string(base) + " " + w5::show(s, 40) + " len=" + std::to_string(s.size()) + " h=" + w5::hex(w5::fnv(s)), w5::isolated([&] { return integer(s, base); }));
		}
	}
	std::vector<std::string> floats = {"0", "-0", "0.0", "-0.0", "1", "-1", "0.1", "-0.1", "0.3", "1.5", "123456789", "1234567890123456789", "0.000001", "1e0", "1e5", "1e22", "1e23", "1e-5", "1e-22", "1e-23",
									   "1e308", "1.7976931348623157e308", "1.7976931348623159e308", "1e309", "-1e309", "1e-307", "2.2250738585072014e-308", "2.2250738585072011e-308", "4.9e-324", "5e-324", "2.4703282292062327e-324",
									   "2.4703282292062328e-324", "1e-324", "1e-400", "1e400", "3.4028235e38", "3.4028236e38", "3.5e38", "1.17549435e-38", "1.4e-45", "7e-46", "7.006492321624085e-46", "1e-46",
									   "0.1e1", "1.e1", ".5", "5.", ".", "-.", "+.5", "1e", "1e+", "1e-", "e5", "1ee5", "1.2.3", "nan", "NaN", "-nan", "inf", "-inf", "infinity", "INF", "0x1p3", "0x.8", "0x1.8p1", "0x",
									   "1_0", "1 ", " 1", "1\n", "1f", "1d", "1L", "+1e+1", "-1E-1", "123456789012345678901234567890", "0.123456789012345678901234567890",
									   "9007199254740993", "9007199254740992.5", "1.00000000000000011102230246251565404236316680908203125", "1.00000000000000011102230246251565404236316680908203126",
									   "0.30000000000000004", "8.5e-5", "4503599627370497.5", "8.98846567431158e307"};
	std::mt19937_64 rng(2026);
	for (int i = 0; i < 300; ++i) {
		// Random decimals of various lengths, with and without exponents.
		std::string s;
		if (rng() & 1) s += '-';
		auto digits = 1 + rng() % 40;
		for (unsigned d = 0; d < digits; ++d) s += static_cast<char>('0' + rng() % 10);
		if (rng() % 3) s.insert(1 + rng() % s.size(), ".");
		if (rng() % 2) s += "e" + std::to_string(static_cast<int>(rng() % 700) - 350);
		floats.push_back(s);
	}
	for (std::size_t n : {20u, 31u, 62u, 63u, 64u, 65u, 66u, 100u, 400u, 800u, 1000u}) {
		floats.push_back(std::string(n, '0') + "5");
		floats.push_back("0." + std::string(n, '0') + "5");
		floats.push_back("0." + std::string(n, '3'));
		floats.push_back(std::string(n, '9'));
		floats.push_back("1" + std::string(n, '0') + "e-" + std::to_string(n));
		floats.push_back("5" + std::string(n, ' '));
		floats.push_back("0." + std::string(n - 2, '1') + "e5");
		floats.push_back(std::string(n, '0') + "." + std::string(n, '0') + "1");
	}
	for (const auto& s : floats) {
		w5::line("float " + w5::show(s, 40) + " len=" + std::to_string(s.size()) + " h=" + w5::hex(w5::fnv(s)), w5::isolated([&] { return floating(s); }));
	}
	return 0;
}
