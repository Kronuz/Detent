// random through the old and the new pins. The sequence differs by design (a new generator, no seed API), so the
// comparison is of what Detent depends on: the ranges Detent draws from (a poll source's start node, the
// full-jitter backoff, the daemon's session id) stay inclusive and in range, and the draws are uniform. The `seq`
// lines print draws, which differ between any two runs of either build.
#include "common.hh"

#include <random.hh>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace {

std::string uniform(std::uint64_t lo, std::uint64_t hi, std::size_t draws) {
	constexpr std::size_t buckets = 16;
	std::vector<std::uint64_t> count(buckets);
	std::uint64_t mn = UINT64_MAX, mx = 0, outside = 0;
	for (std::size_t i = 0; i < draws; ++i) {
		auto v = random_int(lo, hi);
		if (v < lo || v > hi) ++outside;
		mn = std::min(mn, v);
		mx = std::max(mx, v);
		auto span = static_cast<long double>(hi) - static_cast<long double>(lo) + 1;
		auto b = static_cast<std::size_t>((static_cast<long double>(v) - lo) / span * buckets);
		++count[std::min(b, buckets - 1)];
	}
	double expected = static_cast<double>(draws) / buckets, chi = 0;
	if (hi - lo + 1 >= buckets || hi - lo + 1 == 0) {
		for (auto c : count) chi += (c - expected) * (c - expected) / expected;
	}
	// 15 degrees of freedom: chi-square above 50 is a failure at p < 1e-5.
	char b[160];
	std::snprintf(b, sizeof b, "outside=%llu reaches_low=%d reaches_high=%d uniform=%s",
				  static_cast<unsigned long long>(outside), mn == lo, mx == hi, chi < 50 ? "yes" : "NO");
	return b;
}

} // namespace

int main() {
	// The ranges Detent draws from.
	for (auto [lo, hi] : std::vector<std::pair<std::uint64_t, std::uint64_t>>{{0, 0}, {0, 1}, {0, 2}, {0, 7}, {0, 99}, {0, 999999}, {1, UINT64_MAX}, {0, UINT64_MAX}, {5, 5}, {UINT64_MAX - 3, UINT64_MAX}, {1ULL << 63, (1ULL << 63) + 1000}}) {
		w5::line("int [" + std::to_string(lo) + "," + std::to_string(hi) + "]", uniform(lo, hi, 400000));
	}
	// Small ranges hit every value, and only those.
	for (std::uint64_t hi : {1, 2, 3, 6, 10}) {
		std::set<std::uint64_t> seen;
		for (int i = 0; i < 20000; ++i) seen.insert(random_int(0, hi));
		w5::line("int-values [0," + std::to_string(hi) + "]", "distinct=" + std::to_string(seen.size()) + " max=" + std::to_string(*seen.rbegin()));
	}
	// The backoff jitter: random_time(0, backoff) as client.cc draws it.
	for (long long ms : {1LL, 2LL, 100LL, 1000LL, 5000LL, 60000LL}) {
		long long mn = 1LL << 60, mx = -1;
		for (int i = 0; i < 100000; ++i) {
			auto t = random_time(std::chrono::milliseconds{0}, std::chrono::milliseconds{ms}).count();
			mn = std::min(mn, t);
			mx = std::max(mx, t);
		}
		w5::line("time [0," + std::to_string(ms) + "]ms", "in_range=" + std::string(mn >= 0 && mx <= ms ? "yes" : "NO") + " reaches_high=" + (mx == ms ? "yes" : "no"));
	}
	for (auto [lo, hi] : std::vector<std::pair<double, double>>{{0, 1}, {-1, 1}, {10, 20}, {0, 1e9}}) {
		double mn = 1e300, mx = -1e300;
		for (int i = 0; i < 100000; ++i) {
			auto v = random_real(lo, hi);
			mn = std::min(mn, v);
			mx = std::max(mx, v);
		}
		w5::line("real [" + std::to_string(lo) + "," + std::to_string(hi) + "]", "in_range=" + std::string(mn >= lo && mx <= hi ? "yes" : "NO"));
	}
	for (std::size_t len : {0u, 1u, 7u, 64u, 1000u}) {
		std::set<char> alphabet;
		bool length_ok = true;
		for (int i = 0; i < 2000; ++i) {
			auto s = random_string(len);
			length_ok &= s.size() == len;
			alphabet.insert(s.begin(), s.end());
		}
		w5::line("string len=" + std::to_string(len), "length_ok=" + std::string(length_ok ? "yes" : "NO") + " alphabet=" + std::string(alphabet.begin(), alphabet.end()));
	}
	// The sequence itself: two runs of the same build already differ.
	std::string seq;
	for (int i = 0; i < 4; ++i) seq += std::to_string(random_int(0, 999999)) + " ";
	w5::line("seq", seq);
	return 0;
}
