// flat_map differential test against std::map, plus builder and merge checks.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "detent/flat_map.hh"

using detent::flat_map;
using detent::flat_map_builder;

#define CHECK(cond)                                                                      \
	do {                                                                                 \
		if (!(cond)) {                                                                   \
			std::fprintf(stderr, "%s:%d CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			std::abort();                                                                \
		}                                                                                \
	} while (0)

namespace {

using FM = flat_map<std::string, int, std::less<>>;
using SM = std::map<std::string, int, std::less<>>;

void same(const FM& f, const SM& s) {
	CHECK(f.size() == s.size());
	auto it = f.begin();
	for (const auto& [k, v] : s) {
		CHECK(it->first == k && it->second == v);
		++it;
	}
	CHECK(it == f.end());
}

std::string name(unsigned i) { return "key-with-a-long-suffix-past-sso-" + std::to_string(i); }

void differential(unsigned seed) {
	std::mt19937 rng(seed);
	FM f;
	SM s;
	for (int step = 0; step < 4000; ++step) {
		const auto k = name(rng() % 300);
		const int v = static_cast<int>(rng() % 1000);
		switch (rng() % 8) {
		case 0: {
			auto a = f.try_emplace(k, v);
			auto b = s.try_emplace(k, v);
			CHECK(a.second == b.second && a.first->first == k);
			break;
		}
		case 1:
			f[k] = v;
			s[k] = v;
			break;
		case 2: {
			auto a = f.insert_or_assign(k, v);
			auto b = s.insert_or_assign(k, v);
			CHECK(a.second == b.second && a.first->second == v);
			break;
		}
		case 3: {
			auto a = f.emplace(k, v);
			auto b = s.emplace(k, v);
			CHECK(a.second == b.second);
			break;
		}
		case 4:
			CHECK(f.erase(k) == s.erase(k));
			break;
		case 5: {
			const std::string_view sv = k;
			CHECK(f.contains(sv) == s.contains(sv));
			CHECK(f.count(k) == s.count(k));
			auto fi = f.find(sv);
			auto si = s.find(sv);
			CHECK((fi == f.end()) == (si == s.end()));
			if (si != s.end())
				CHECK(fi->second == si->second);
			auto fl = f.lower_bound(sv);
			auto sl = s.lower_bound(sv);
			CHECK((fl == f.end()) == (sl == s.end()));
			if (sl != s.end())
				CHECK(fl->first == sl->first);
			break;
		}
		case 6:
			if (!s.empty()) {
				const auto n = rng() % s.size();
				auto fi = std::next(f.begin(), static_cast<std::ptrdiff_t>(n));
				auto si = std::next(s.begin(), static_cast<std::ptrdiff_t>(n));
				auto fnext = f.erase(fi);
				auto snext = s.erase(si);
				CHECK((fnext == f.end()) == (snext == s.end()));
			}
			break;
		default:
			if (s.size() > 4 && rng() % 16 == 0) {
				const auto a = rng() % (s.size() / 2);
				const auto n = rng() % (s.size() / 2);
				f.erase(std::next(f.cbegin(), static_cast<std::ptrdiff_t>(a)),
				        std::next(f.cbegin(), static_cast<std::ptrdiff_t>(a + n)));
				s.erase(std::next(s.begin(), static_cast<std::ptrdiff_t>(a)),
				        std::next(s.begin(), static_cast<std::ptrdiff_t>(a + n)));
			}
			break;
		}
		if (step % 97 == 0)
			same(f, s);
	}
	same(f, s);
	FM copy = f;
	CHECK(copy == f);
	f.swap(copy);
	f.clear();
	CHECK(f.empty() && copy.size() == s.size());
	CHECK(!(copy == f));
}

void at_and_back_insert() {
	FM f;
	for (unsigned i = 0; i < 100; ++i)
		f.try_emplace(name(1000 + i), static_cast<int>(i));
	// Ascending keys under the lexicographic order of name() with equal
	// length stay on the push_back path; check order and at().
	CHECK(std::is_sorted(f.begin(), f.end(), [](const auto& a, const auto& b) { return a.first < b.first; }));
	CHECK(f.at(name(1007)) == 7);
	CHECK(f.at(std::string_view(name(1042))) == 42);
	bool threw = false;
	try {
		(void)f.at("absent");
	} catch (const std::out_of_range&) {
		threw = true;
	}
	CHECK(threw);
}

// Builder: result equals std::map insertion when keys are unique; the first
// duplicate in input order is reported otherwise.
void builder() {
	std::mt19937 rng(7);
	for (int round = 0; round < 300; ++round) {
		const unsigned n = rng() % 40;
		const unsigned span = 1 + rng() % 60;
		const bool ascending = rng() % 4 == 0;
		std::vector<unsigned> keys(n);
		for (unsigned i = 0; i < n; ++i)
			keys[i] = ascending ? i * 3 : rng() % span;
		FM f;
		flat_map_builder<std::string, int, std::less<>> b(f);
		b.reserve(n);
		SM s;
		std::optional<std::size_t> want_dup;
		for (unsigned i = 0; i < n; ++i) {
			if (!s.try_emplace(name(keys[i]), static_cast<int>(i)).second && !want_dup)
				want_dup = i;
			b.push(name(keys[i]), static_cast<int>(i));
		}
		auto got = b.finish();
		CHECK(got == want_dup);
		if (!got)
			same(f, s);
		else
			CHECK(b.entry(*got).first == name(keys[*got]));
	}
}

void merge() {
	FM a, b;
	for (int i = 0; i < 20; ++i)
		a.try_emplace(name(i * 2), i);
	for (int i = 0; i < 20; ++i)
		b.try_emplace(name(i * 3), -i);
	SM both, a_only, b_only;
	flat_map_merge(
		a, b, [&](const auto& e) { a_only.emplace(e); }, [&](const auto& e) { b_only.emplace(e); },
		[&](const auto& x, const auto&) { both.emplace(x); });
	for (const auto& [k, v] : a)
		CHECK((b.contains(k) ? both : a_only).contains(k));
	for (const auto& [k, v] : b)
		CHECK((a.contains(k) ? both : b_only).contains(k));
	CHECK(a_only.size() + both.size() == a.size());
	CHECK(b_only.size() + both.size() == b.size());
}

// Repeated reserve calls with slowly rising totals must not reallocate
// on every call, or bulk parsing with many reserve points goes quadratic.
void builder_reserve_is_geometric() {
	detent::flat_map<std::string, int> map;
	detent::flat_map_builder<std::string, int, std::less<>> builder(map);
	std::size_t reallocations = 0, capacity = map.capacity();
	for (int i = 0; i < 100000; ++i) {
		builder.reserve(builder.size() + 1);
		builder.push(std::to_string(1000000 - i), i);
		if (map.capacity() != capacity) {
			++reallocations;
			capacity = map.capacity();
		}
	}
	CHECK(reallocations <= 40);
	CHECK(!builder.finish());
	CHECK(map.size() == 100000 && std::is_sorted(map.begin(), map.end(), [](const auto& l, const auto& r) { return l.first < r.first; }));
}

void bulk_erase() {
	detent::flat_map<std::string, int> map;
	for (int i = 0; i < 1000; ++i)
		map.try_emplace(std::to_string(1000 + i), i);
	CHECK(erase_if(map, [](const auto& entry) { return entry.second % 4 != 0; }) == 750);
	CHECK(map.size() == 250);
	int expected = 0;
	for (const auto& [key, value] : map) {
		CHECK(value == expected && key == std::to_string(1000 + expected));
		expected += 4;
	}
}

} // namespace

int main() {
	for (unsigned seed = 1; seed <= 20; ++seed)
		differential(seed);
	at_and_back_insert();
	builder();
	merge();
	bulk_erase();
	builder_reserve_is_geometric();
	std::puts("PASS flat_map");
	return 0;
}
