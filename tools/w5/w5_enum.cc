// enum-reflection through the old and the new pins. http-parser names its methods, statuses and errors with this
// library. Every member name (in several cases), near misses, random strings and strings forged to share a member's
// 64-bit hash go through enum_type; a forged string is the one input whose answer changes (a member before, a miss
// after).
#include "common.hh"

#include <http_parser.h>

#include <hashes.hh>

#include <random>
#include <stdexcept>
#include <string>
#include <vector>

ENUM_CLASS(W5Role, int, User, Admin, Guest, Internal_Server_Error, x9, ABCDEFGH)

namespace {

constexpr std::uint64_t P = 0x9e3779b97f4a7c15ULL;

std::uint64_t inv_mul(std::uint64_t a) {
	std::uint64_t x = a;
	for (int i = 0; i < 6; ++i) x *= 2 - a * x;
	return x;
}
std::uint64_t inv_xorshift(std::uint64_t h, int s) {
	std::uint64_t x = h;
	for (int i = 0; i < 64 / s + 1; ++i) x = h ^ (x >> s);
	return x;
}
bool fold_fixed(unsigned char b) {
	return !(b >= 'A' && b <= 'Z');
}

// A 15-byte string whose wordwise_ci hash equals `target`'s (enum-reflection's names_test).
std::string forge(std::string_view target, std::uint64_t seed) {
	std::mt19937_64 rng(seed);
	const std::uint64_t T = wordwise_ci::hash(target.data(), target.size());
	const std::size_t L = 15;
	const std::uint64_t h0 = 0x100000001b3ULL ^ (static_cast<std::uint64_t>(L) * P);
	const std::uint64_t before_mul = inv_xorshift(T, 32) * inv_mul(P);
	for (;;) {
		std::uint64_t w = rng();
		bool ok = true;
		for (int k = 0; k < 8; ++k) ok &= fold_fixed(static_cast<unsigned char>(w >> (8 * k))) && (w >> (8 * k) & 0xff) < 0x80;
		if (!ok) continue;
		std::uint64_t h = (h0 ^ w) * P;
		h ^= h >> 29;
		std::uint64_t tail = h ^ before_mul;
		if (tail >> 56) continue;
		bool tok = true;
		for (int k = 0; k < 7; ++k) tok &= fold_fixed(static_cast<unsigned char>(tail >> (8 * k)));
		if (!tok) continue;
		char buf[16];
		for (int k = 0; k < 8; ++k) buf[k] = static_cast<char>(w >> (8 * k));
		for (int k = 0; k < 7; ++k) buf[8 + k] = static_cast<char>(tail >> (8 * k));
		return std::string(buf, L);
	}
}

template <typename E>
std::string lookup(std::string_view name) {
	try {
		return "member " + std::to_string(static_cast<long long>(enum_type<E>(name)));
	} catch (const std::out_of_range&) {
		return "out_of_range";
	}
}

template <typename E, typename Values>
void probe(const char* label, const Values& values) {
	std::mt19937_64 rng(77);
	std::vector<std::string> names;
	for (auto v : values) names.emplace_back(enum_name(v));
	for (const auto& name : names) {
		std::string upper = name, lower = name, mixed = name;
		for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		for (std::size_t i = 0; i < mixed.size(); i += 2) mixed[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(mixed[i])));
		std::vector<std::string> probes = {name, upper, lower, mixed, name + "x", "x" + name, name.substr(0, name.size() / 2), name + name, name + std::string(1, '\0'), " " + name, name + " "};
		for (std::size_t i = 0; i < name.size(); ++i) {
			std::string drop = name;
			drop.erase(i, 1);
			probes.push_back(drop);
			std::string swap = name;
			swap[i] = static_cast<char>(swap[i] ^ 1);
			probes.push_back(swap);
		}
		for (int k = 0; k < 4; ++k) probes.push_back(forge(name, 100 + k));
		for (const auto& p : probes) {
			std::string result = lookup<E>(p);
			if constexpr (requires { enum_try_type<E>(std::string_view()); }) {
				auto t = enum_try_type<E>(p);
				result += t ? " try=member" : " try=nullopt";
			}
			w5::line(std::string("enum ") + label + " " + w5::show(p, 40) + " h=" + w5::hex(w5::fnv(p)), result);
		}
	}
	for (int i = 0; i < 2000; ++i) {
		std::string s(rng() % 20, '\0');
		for (auto& c : s) c = static_cast<char>(rng());
		w5::line(std::string("enum-random ") + label + " " + std::to_string(i), lookup<E>(s));
	}
	for (std::size_t n : {0u, 1u, 100u, 10000u}) w5::line(std::string("enum-length ") + label + " " + std::to_string(n), lookup<E>(std::string(n, 'a')));
}

} // namespace

int main() {
	probe<W5Role>("W5Role", detail_W5Role::values);
	probe<http_method>("http_method", detail_http_method::values);
	probe<http_status>("http_status", detail_http_status::values);
	probe<http_errno>("http_errno", detail_http_errno::values);
	// The strings http-parser derives from the same tables.
	for (int m = 0; m < 50; ++m) w5::line("method_str " + std::to_string(m), http_method_str(static_cast<http_method>(m)));
	for (int s : {100, 101, 200, 204, 301, 304, 400, 404, 418, 500, 503}) w5::line("status_str " + std::to_string(s), http_status_str(static_cast<http_status>(s)));
	return 0;
}
