// Golden hash-bucket vectors for the Section 8 stable-hash rule. Generated
// once, independently, via a small Python script using hashlib.sha256 (NOT
// derived from this C++ implementation), per the spec's testing plan: "commit
// at least 20 input/digest-prefix/bucket vectors generated once by an
// independent tool" and "run the same vectors in [every] implementation".
//
// bucket = big-endian-u64(SHA256(name || 0x00 || hash_val))[0..8] % 100
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "detent/hashing.hh"

namespace {

struct Vector {
	std::string_view name;
	std::string_view hash_val;
	std::uint64_t expected_bucket;
};

constexpr Vector kVectors[] = {
	{"checkout:killswitch_new_path", "user-1", 64},
	{"checkout:killswitch_new_path", "user-2", 3},
	{"checkout:killswitch_new_path", "user-42", 7},
	{"search:max_results", "user-42", 21},
	{"search:max_results", "tenant-abc", 0},
	{"a:b", "", 47},
	{"a:b", "x", 7},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 21},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 39},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 98},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 30},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 56},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 80},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 37},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 19},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 49},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 89},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 96},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 49},
	{"rollout:flag", "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh", 94},
	{"a:b", std::string_view("\000", 1), 26},
	{"a:b", std::string_view("\150\000\172", 3), 95},
	{"a:b", std::string_view("\377\000\200", 3), 75},
	{"a:b", std::string_view("", 0), 47},
};

} // namespace

int main() {
	for (const auto &v : kVectors) {
		auto bucket = detent::stable_bucket(v.name, v.hash_val);
		assert(bucket == v.expected_bucket);
		assert(bucket < 100);
	}

	assert(detent::stable_bucket(std::string_view{}, std::string_view{}) == 92);

	// Independence: changing only the knob name changes the cohort for the
	// same hash_val, for at least one of these vectors (Section 13).
	assert(detent::stable_bucket("checkout:killswitch_new_path", "user-42") !=
		   detent::stable_bucket("search:max_results", "user-42"));

	// Stickiness: identical (name, hash_val) always yields the same bucket,
	// across repeated calls in this process (a proxy for "across restarts and
	// hosts", which a single-process unit test cannot itself exercise).
	for (int i = 0; i < 100; ++i) {
		assert(detent::stable_bucket("a:b", "x") == 7);
	}

	std::puts("hash golden vectors OK");
	return 0;
}
