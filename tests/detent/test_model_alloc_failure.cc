// Allocation-denial sweep over the four public model tables: every
// allocation position of insert, erase and copy is refused in turn, and the
// table must keep its contents and release every allocation. Legal names
// outside the small-string buffer exercise key relocation inside full nodes.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <vector>

#include "detent/model.hh"
#include "detent/overrides.hh"

namespace {
long budget = -1, live = 0;
}

void* operator new(std::size_t bytes) {
	if (budget == 0)
		throw std::bad_alloc();
	if (budget > 0)
		--budget;
	if (auto p = std::malloc(bytes ? bytes : 1)) {
		++live;
		return p;
	}
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
	if (p)
		--live;
	std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
	if (p)
		--live;
	std::free(p);
}

using namespace detent;

namespace {

bool long_names = false;

std::string key(unsigned i) {
	char digits[16];
	std::snprintf(digits, sizeof digits, "%08u", i);
	if (!long_names)
		return std::string("k") + digits;
	std::string name(200, 'k');
	name.replace(192, 8, digits);
	return name;
}

std::string group(unsigned g) {
	std::string name(long_names ? 128 : 6, 'g');
	name.back() = static_cast<char>('0' + g);
	return name;
}

BoolDef bool_def(unsigned i) {
	BoolDef def;
	def.default_value = i & 1;
	for (unsigned g = 0; g < 3; ++g) {
		if (g == 1)
			def.overrides.try_emplace(group(g), GroupRollout{static_cast<std::uint8_t>(i % 100)});
		else
			def.overrides.try_emplace(group(g), bool((i + g) & 1));
	}
	return def;
}

template <class V>
V make(unsigned i);
template <>
KnobDef make<KnobDef>(unsigned i) {
	return (i % 4 == 3) ? KnobDef(IntDef{std::int64_t(i)}) : KnobDef(bool_def(i));
}
template <>
std::optional<KnobDef> make<std::optional<KnobDef>>(unsigned i) {
	return (i % 5 == 4) ? std::nullopt : std::optional<KnobDef>(make<KnobDef>(i));
}
template <>
GroupOverride make<GroupOverride>(unsigned i) {
	return (i & 1) ? GroupOverride(GroupRollout{static_cast<std::uint8_t>(i % 100)}) : GroupOverride(bool(i & 2));
}
template <>
OverrideValue make<OverrideValue>(unsigned i) {
	return (i & 1) ? OverrideValue(std::int64_t(i)) : OverrideValue(bool(i & 2));
}

[[noreturn]] void fail(const char* what, const char* op, unsigned n, long b) {
	std::fprintf(stderr, "FAILED %s: op=%s n=%u budget=%ld long_names=%d\n", what, op, n, b, int(long_names));
	std::abort();
}

template <class Map>
void verify(const Map& m, const std::map<std::string, typename Map::mapped_type>& want, const char* op, unsigned n, long b) {
	if (m.size() != want.size())
		fail("size", op, n, b);
	auto it = m.begin();
	for (const auto& [k, v] : want) {
		if (it == m.end() || it->first != k || !(it->second == v))
			fail("content", op, n, b);
		++it;
	}
	if (it != m.end())
		fail("extra entries", op, n, b);
}

template <class Map>
void sweep(const char* op, unsigned max_n) {
	using V = typename Map::mapped_type;
	for (unsigned n = 0; n <= max_n; ++n) {
		for (int random = 0; random < 2; ++random) {
			for (int where = 0; where < 4; ++where) {
				for (long b = 0;; ++b) {
					const long base = live;
					bool refused = false;
					{
						Map m;
						std::map<std::string, V> want;
						std::vector<unsigned> ids(n);
						for (unsigned i = 0; i < n; ++i)
							ids[i] = i * 4 + 1;
						if (random)
							std::shuffle(ids.begin(), ids.end(), std::mt19937(n * 7919u + 1));
						for (auto id : ids) {
							m.try_emplace(key(id), make<V>(id));
							want.try_emplace(key(id), make<V>(id));
						}
						const unsigned target = where == 0 ? 0 : where == 1 ? n * 2 + 2 : where == 2 ? n * 4 + 5 : n + 3;
						budget = b;
						try {
							if (!std::strcmp(op, "insert")) {
								auto value = make<V>(target);
								m.try_emplace(key(target), std::move(value));
								budget = -1;
								want.try_emplace(key(target), make<V>(target));
							} else if (!std::strcmp(op, "erase")) {
								if (n == 0) {
									budget = -1;
									break;
								}
								auto victim = want.begin();
								std::advance(victim, where == 0 ? 0 : where == 1 ? n / 2 : where == 2 ? n - 1 : n / 3);
								m.erase(victim->first);
								budget = -1;
								want.erase(victim);
							} else {
								Map copy(m);
								budget = -1;
								verify(copy, want, op, n, b);
							}
						} catch (const std::bad_alloc&) {
							refused = true;
						}
						budget = -1;
						verify(m, want, op, n, b);
					}
					if (live != base)
						fail("leak", op, n, b);
					if (!refused)
						break;
				}
			}
		}
	}
}

template <class Map>
void sweep_all(unsigned max_n) {
	for (const char* op : {"insert", "erase", "copy"})
		sweep<Map>(op, max_n);
}

} // namespace

int main() {
	for (bool lng : {false, true}) {
		long_names = lng;
		sweep_all<decltype(Snapshot::knobs)>(24);
		sweep_all<decltype(SnapshotPatch::knobs)>(24);
		sweep_all<decltype(BoolDef::overrides)>(24);
		sweep_all<decltype(OverrideTable::values)>(24);
	}
	std::puts("PASS model allocation failure sweep");
	return 0;
}
