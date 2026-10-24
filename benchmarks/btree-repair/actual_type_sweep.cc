// Allocation-denial sweep over Detent's four actual model tables.
// usage: actual_sweep KIND(knobs|patch|group|cli) OP(insert|erase|copy|destroy) short|long [maxn]
#include <detent/model.hh>
#include <detent/overrides.hh>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <vector>

#ifdef STD_MAP
#include <map>
template <class K, class V, class C> using MAPT = std::map<K, V, C>;
#else
template <class K, class V, class C> using MAPT = btree::map<K, V, C>;
#endif
namespace { long budget = -1, live = 0; }
void* operator new(std::size_t n) {
	if (budget == 0) throw std::bad_alloc();
	if (budget > 0) --budget;
	if (auto p = std::malloc(n ? n : 1)) { ++live; return p; }
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (p) --live; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (p) --live; std::free(p); }

using namespace detent;
static bool lng = false;
static std::string key(unsigned i) {
	char b[16];
	std::snprintf(b, sizeof b, "%08u", i);
	if (!lng) return std::string("k") + b;
	std::string s(200, 'k');
	s.replace(192, 8, b);
	return s;
}
static std::string group(unsigned g) {
	std::string s(lng ? 128 : 6, 'g');
	s.back() = char('0' + g);
	return s;
}
static BoolDef bool_def(unsigned i) {
	BoolDef d;
	d.default_value = i & 1;
	for (unsigned g = 0; g < 3; ++g) {
		if (g == 1) d.overrides.try_emplace(group(g), GroupRollout{std::uint8_t(i % 100)});
		else d.overrides.try_emplace(group(g), bool((i + g) & 1));
	}
	return d;
}

template <class V> struct Traits;
template <> struct Traits<KnobDef> {
	using Map = MAPT<KnobName, KnobDef, std::less<>>;
	static KnobDef make(unsigned i) { return (i % 4 == 3) ? KnobDef(IntDef{std::int64_t(i)}) : KnobDef(bool_def(i)); }
};
template <> struct Traits<std::optional<KnobDef>> {
	using Map = MAPT<KnobName, std::optional<KnobDef>, std::less<>>;
	static std::optional<KnobDef> make(unsigned i) {
		if (i % 5 == 4) return std::nullopt;
		return Traits<KnobDef>::make(i);
	}
};
template <> struct Traits<GroupOverride> {
	using Map = MAPT<std::string, GroupOverride, std::less<>>;
	static GroupOverride make(unsigned i) { return (i & 1) ? GroupOverride(GroupRollout{std::uint8_t(i % 100)}) : GroupOverride(bool(i & 2)); }
};
template <> struct Traits<OverrideValue> {
	using Map = MAPT<std::string, OverrideValue, std::less<>>;
	static OverrideValue make(unsigned i) { return (i & 1) ? OverrideValue(std::int64_t(i)) : OverrideValue(bool(i & 2)); }
};

static int bad = 0, refused_total = 0, attempts = 0;
static const char* g_ctx = "";
#define FAIL(...) do { ++bad; std::printf("VIOLATION [%s] ", g_ctx); std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); } while (0)

template <class V> using Oracle = std::map<std::string, V>;

template <class V>
static bool verify(typename Traits<V>::Map& m, const Oracle<V>& want) {
	if (m.size() != want.size()) { FAIL("size %zu != %zu", std::size_t(m.size()), want.size()); return false; }
	auto it = m.begin();
	std::size_t n = 0;
	for (auto& [k, v] : want) {
		if (it == m.end()) { FAIL("iteration ended early at %zu", n); return false; }
		if (it->first != k) { FAIL("key mismatch at %zu", n); return false; }
		if (!(it->second == v)) { FAIL("value mismatch at %zu", n); return false; }
		++it; ++n;
	}
	if (it != m.end()) { FAIL("iteration extra"); return false; }
	for (auto& [k, v] : want) {
		auto f = m.find(k);
		if (f == m.end() || !(f->second == v)) { FAIL("find miss"); return false; }
	}
	return true;
}

template <class V>
static void build(typename Traits<V>::Map& m, Oracle<V>& want, unsigned n, bool random, unsigned skip_slots) {
	std::vector<unsigned> ids(n);
	for (unsigned i = 0; i < n; ++i) ids[i] = i * skip_slots + 1;
	if (random) std::shuffle(ids.begin(), ids.end(), std::mt19937(n * 7919u + 1));
	for (auto id : ids) {
		m.try_emplace(key(id), Traits<V>::make(id));
		want.try_emplace(key(id), Traits<V>::make(id));
	}
}

// Returns number of positions that refused; checks invariants after each.
template <class V>
static void run(const char* op, unsigned maxn) {
	using M = typename Traits<V>::Map;
	std::vector<unsigned> sizes;
	for (unsigned n = 0; n <= maxn; n += (n < 80 ? 1 : n < 400 ? 7 : 61)) sizes.push_back(n);
	for (unsigned n : sizes) for (int random = 0; random < 2; ++random) {
		const unsigned slots = 4;
		for (int where = 0; where < 4; ++where) {
			char ctx[96];
			std::snprintf(ctx, sizeof ctx, "%s n=%u random=%d where=%d", op, n, random, where);
			g_ctx = ctx;
			for (long b = 0; b < 4096; ++b) {
				const long base = live;
				bool refused = false, outer_done = false;
				{
					M m; Oracle<V> want;
					build<V>(m, want, n, random, slots);
					const long pre = live;
					unsigned target = where == 0 ? 0 : where == 1 ? (n ? n * slots / 2 + 2 : 2) : where == 2 ? n * slots + 5 : (n ? n * slots / 3 + 3 : 3);
					budget = b;
					try {
						if (!strcmp(op, "insert")) {
							auto id = target;
							auto val = Traits<V>::make(id);
							m.try_emplace(key(id), std::move(val));
							budget = -1;
							want.try_emplace(key(id), Traits<V>::make(id));
						} else if (!strcmp(op, "erase")) {
							if (n == 0) { budget = -1; outer_done = true; goto leave; }
							auto it = want.begin();
							std::advance(it, std::min<std::size_t>(where == 0 ? 0 : where == 1 ? n / 2 : where == 2 ? n - 1 : n / 3, n - 1));
							m.erase(it->first);
							want.erase(it);
						} else if (!strcmp(op, "copy")) {
							M c(m);
							budget = -1;
							verify<V>(c, want);
						} else if (!strcmp(op, "assign")) {
							M c;
							c = m;
							budget = -1;
							verify<V>(c, want);
						}
					} catch (const std::bad_alloc&) { refused = true; }
					budget = -1;
					outer_done = !refused;
					if (refused) { ++refused_total; }
					++attempts;
					if (!strcmp(op, "insert") || !strcmp(op, "erase")) verify<V>(m, want);
					(void)pre;
				}
			leave:
				if (live != base) FAIL("leak: %ld live allocations unreleased (b=%ld)", live - base, b);
				if (outer_done) break;
			}
		}
	}
}

int main(int argc, char** argv) {
	if (argc < 4) return 2;
	const char *kind = argv[1], *op = argv[2];
	lng = !strcmp(argv[3], "long");
	unsigned maxn = argc > 4 ? unsigned(std::atoi(argv[4])) : 600;
	if (!strcmp(kind, "knobs")) run<KnobDef>(op, maxn);
	else if (!strcmp(kind, "patch")) run<std::optional<KnobDef>>(op, maxn);
	else if (!strcmp(kind, "group")) run<GroupOverride>(op, maxn);
	else if (!strcmp(kind, "cli")) run<OverrideValue>(op, maxn);
	else return 2;
	std::printf("{\"kind\":\"%s\",\"op\":\"%s\",\"keys\":\"%s\",\"attempts\":%d,\"refusals\":%d,\"violations\":%d}\n", kind, op, argv[3], attempts, refused_total, bad);
	return bad ? 1 : 0;
}
