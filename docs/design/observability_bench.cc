// observability_bench: the warm-lookup cost of each knob-usage counting option (docs/design/observability.md
// D2), measured around reader S6's real lookup. observability-bench.sh built it against `Detent:b1d281c`,
// whose C reader API (`detent/reader.h`) this file still calls; its definitions are the one knob type.
//
//   observability_bench --store DIR [--knobs N] [--reps N] [--calls N] [--threads 1,8]
//                       [--arms a,b,...] [--scattered N] [--map-dir DIR] [--soak SECONDS]
//                       [--interleave 0|1] [--bare 0|1]
//
// Each case prints one JSON line. A case times batches of 64 calls with steady_clock and reports the
// per-call mean of each batch, so the clock costs under 1 ns per call. The counting code runs after
// the lookup in this file, not inside the core: the core already holds the stripe this file
// recomputes, so a striped arm here costs about one hash more than it would inside the core.
//
// `--interleave 1` is OB1's paired method (D2, A8): each thread alternates 64-lookup blocks of the arm and of
// `none`, and a case reports the median over all threads' block pairs of (arm - none). The `pos_*`,
// `ever_*` and `dm_cache` arms index by the knob's definition position, which each name carries from
// setup (the core would derive it from the definition it found; A8). `--bare 1` drops the lookup, so
// a case times the counting code alone. Every knob is one int64 definition read through `detent_get`
// (docs/design/one-knob-type.md): half are plain integers, half serve a 50% rollout value.
#include "detent/json_schema.hh"
#include "detent/reader.h"
#include "detent_testing/generation_writer.hh"
#include <metrics.hh>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
thread_local bool counting = false; // only inside a timed loop
std::atomic<std::uint64_t> allocations{0};
} // namespace
void *operator new(std::size_t bytes) {
	if (counting)
		allocations.fetch_add(1, std::memory_order_relaxed);
	if (void *p = std::malloc(bytes ? bytes : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t batch = 64, stripes = 64, cells_per_line = 8;
constexpr auto relaxed = std::memory_order_relaxed;

// The reader core's stripe (src/detent_reader/core.cc:142-144 at b1d281c): a hash of a stack address.
inline std::size_t stripe_of(const void *frame) noexcept {
	return std::size_t((std::uintptr_t(frame) >> 12) * 0x9e3779b97f4a7c15ull >> 58);
}
// Word-at-a-time 64-bit hash of a knob name (at most 200 bytes).
inline std::uint64_t name_hash(const char *p, std::size_t n) noexcept {
	std::uint64_t h = 0x9e3779b97f4a7c15ull ^ n;
	for (; n >= 8; p += 8, n -= 8) {
		std::uint64_t w;
		std::memcpy(&w, p, 8);
		h = (h ^ w) * 0xbf58476d1ce4e5b9ull;
		h ^= h >> 31;
	}
	if (n) {
		std::uint64_t w = 0;
		std::memcpy(&w, p, n);
		h = (h ^ w) * 0x94d049bb133111ebull;
		h ^= h >> 29;
	}
	h *= 0xbf58476d1ce4e5b9ull;
	return h ^ (h >> 32);
}
template <class T> inline void escape(const T &value) noexcept { asm volatile("" : : "r"(&value) : "memory"); }

// rows × (capacity + 1) lines of 8 counters; the last slot of each row takes table overflow.
struct Cells {
	std::uint64_t *base = nullptr;
	std::size_t rows = 0, slots = 0, bytes = 0;
	bool mapped = false;
	void init(std::size_t r, std::size_t capacity, const std::string &map_path) {
		rows = r;
		slots = capacity + 1;
		bytes = rows * slots * cells_per_line * sizeof(std::uint64_t);
		if (map_path.empty()) {
			base = static_cast<std::uint64_t *>(::operator new(bytes, std::align_val_t(64)));
		} else {
			const int fd = ::open(map_path.c_str(), O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0600);
			if (fd < 0 || ::ftruncate(fd, off_t(bytes)) != 0)
				throw std::runtime_error("cannot create " + map_path);
			void *p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			::close(fd);
			if (p == MAP_FAILED)
				throw std::runtime_error("cannot map " + map_path);
			base = static_cast<std::uint64_t *>(p);
			mapped = true;
		}
		clear();
	}
	std::uint64_t *line(std::size_t row, std::size_t slot) noexcept { return base + (row * slots + slot) * cells_per_line; }
	void clear() noexcept { std::memset(base, 0, bytes); }
	std::uint64_t total() const noexcept {
		std::uint64_t sum = 0;
		for (std::size_t i = 0; i != bytes / sizeof(std::uint64_t); ++i)
			sum += __atomic_load_n(&base[i], __ATOMIC_RELAXED);
		return sum;
	}
};
// Open-addressed name table: a slot per knob, claimed by compare-and-swap on first sight.
struct Table {
	std::vector<std::atomic<std::uint64_t>> keys;
	std::size_t mask = 0;
	unsigned shift = 0;
	explicit Table(std::size_t capacity) : keys(capacity), mask(capacity - 1) {
		shift = 64 - unsigned(__builtin_ctzll(capacity));
	}
	std::size_t find(std::uint64_t key) noexcept {
		key |= 1;
		std::size_t i = std::size_t((key * 0x9e3779b97f4a7c15ull) >> shift);
		for (std::size_t probe = 0; probe != 32; ++probe, i = (i + 1) & mask) {
			auto k = keys[i].load(std::memory_order_acquire);
			if (k == key)
				return i;
			if (k == 0 && (keys[i].compare_exchange_strong(k, key, std::memory_order_acq_rel) || k == key))
				return i;
		}
		return mask + 1; // overflow
	}
	void clear() noexcept {
		for (auto &k : keys)
			k.store(0, relaxed);
	}
};
struct alignas(64) StripeLine {
	std::uint64_t cell[cells_per_line];
};
struct Event {
	const char *name;
	std::size_t size;
	int status, arm;
	std::int64_t value;
};
using Hook = void (*)(void *, const Event *) noexcept;
void noop_hook(void *, const Event *event) noexcept { asm volatile("" : : "r"(event) : "memory"); }

alignas(64) std::atomic<std::uint64_t> shared_counter{0};
alignas(64) kronuz::metrics::AtomicCounter metrics_counter;
alignas(64) kronuz::metrics::ContendedCounter contended_counter;
StripeLine stripe_lines[stripes];
thread_local std::uint64_t thread_count = 0;
alignas(64) std::atomic<std::uint64_t> thread_total{0};
alignas(64) std::atomic<Hook> hook{nullptr};
Table *table = nullptr;
Cells shared_cells, rows8, rows64, rows8_mapped;

// A8's position-indexed arms: `positions` knob definitions, each name's index in [0, positions).
std::size_t positions = 0;
std::atomic<std::uint64_t> *pos_counts = nullptr;        // one shared row
std::uint32_t *stripe_counts = nullptr;                   // 64 rows, relaxed fetch_add
std::atomic<std::uint64_t> *ever_bits = nullptr;          // one bit per definition
thread_local std::uint32_t *tls_counts = nullptr;         // one row per thread, plain load and store
std::mutex tls_mutex;
std::vector<std::uint32_t *> tls_rows;
std::atomic<std::uint64_t> tls_allocations{0};
struct alignas(64) DirectMapped {                         // 8 (position + 1, count) entries in one line
	std::uint32_t key[8], count[8];
};
thread_local DirectMapped dm_line{};
// The `*_tls` arms' countdown: one per thread, so no two threads share it (the stripe's can, at 64 threads).
thread_local std::uint64_t tls_countdown = 63;
std::uint32_t *tls_allocate() noexcept {
	auto *row = static_cast<std::uint32_t *>(std::calloc(positions, sizeof(std::uint32_t)));
	if (!row)
		std::abort();
	tls_allocations.fetch_add(1, relaxed);
	std::lock_guard lock(tls_mutex);
	tls_rows.push_back(row);
	return tls_counts = row;
}
void dm_flush() noexcept {
	for (std::size_t i = 0; i != 8; ++i)
		if (dm_line.count[i])
			pos_counts[dm_line.key[i] - 1].fetch_add(dm_line.count[i], relaxed);
	dm_line = {};
}

#define ARMS(X)                                                                                                       \
	X(none) X(none2) X(shared) X(metrics_atomic) X(metrics_contended) X(stripe_rmw) X(stripe_plain) X(tls) X(knob_probe) X(knob_shared)     \
	    X(knob_rows8) X(knob_rows64) X(knob_ptr_rows8) X(knob_sampled) X(knob_rows8_mapped) X(hook_off) X(hook_on)   \
	        X(pos_shared) X(pos_stripe_rmw) X(pos_tls) X(ever_bit) X(ever_sampled) X(dm_cache) X(knob_tls) X(es_tls)  \
	            X(es_one) X(es_tls_one)
enum class Arm {
#define X(a) a,
	ARMS(X)
#undef X
};
constexpr const char *arm_names[] = {
#define X(a) #a,
    ARMS(X)
#undef X
};
constexpr std::size_t arm_count = sizeof arm_names / sizeof *arm_names;
static_assert(std::size_t(Arm::hook_on) < arm_count);

struct Name {
	std::string text;
	std::uint32_t pos = 0; // definition position: 2 × index, + 1 for the rollout knob
	detent_str str() const noexcept { return {text.data(), text.size()}; }
};
// Relaxed atomic access to a plain word (the cells may live in a shared file mapping).
struct Word {
	std::uint64_t *p;
	std::uint64_t load() const noexcept { return __atomic_load_n(p, __ATOMIC_RELAXED); }
	void store(std::uint64_t v) const noexcept { __atomic_store_n(p, v, __ATOMIC_RELAXED); }
	void add(std::uint64_t v) const noexcept { (void)__atomic_fetch_add(p, v, __ATOMIC_RELAXED); }
};
inline Word at(std::uint64_t &word) noexcept { return {&word}; }

// The sampled path's slow half, out of line so the per-lookup half stays a load, a test and a store.
[[gnu::noinline, gnu::cold]] void sample(const Name &name, unsigned cell) noexcept {
	const auto slot = table->find(name_hash(name.text.data(), name.text.size()));
	at(shared_cells.line(0, slot)[cell]).add(64);
}
inline unsigned cell_of(detent_status status, std::int64_t value) noexcept {
	return status != DETENT_OK ? 6u : (value != 0 ? 1u : 0u);
}
// The one-branch arms' slow half: sets the bit if clear, and reloads and samples if the countdown ran out.
[[gnu::noinline, gnu::cold]] void ever_slow(const Name &name, detent_status status, std::int64_t value,
                                            std::uint64_t *countdown, std::uint64_t left) noexcept {
	auto &word = ever_bits[name.pos >> 6];
	const auto bit = std::uint64_t(1) << (name.pos & 63);
	if (!(word.load(relaxed) & bit))
		word.fetch_or(bit, relaxed);
	if (left) {
		at(*countdown).store(left - 1);
	} else {
		at(*countdown).store(63);
		sample(name, cell_of(status, value));
	}
}
template <Arm A> inline void count(const Name &name, detent_status status, std::int64_t value) noexcept {
	const unsigned cell = cell_of(status, value);
	if constexpr (A == Arm::shared) {
		shared_counter.fetch_add(1, relaxed);
	} else if constexpr (A == Arm::metrics_atomic) {
		metrics_counter.add();
	} else if constexpr (A == Arm::metrics_contended) {
		contended_counter.add();
	} else if constexpr (A == Arm::stripe_rmw || A == Arm::stripe_plain || A == Arm::knob_sampled ||
	                     A == Arm::knob_rows8 || A == Arm::knob_rows64 || A == Arm::knob_ptr_rows8 ||
	                     A == Arm::knob_rows8_mapped) {
		const char frame = 0;
		const auto stripe = stripe_of(&frame);
		if constexpr (A == Arm::stripe_rmw) {
			at(stripe_lines[stripe].cell[cell]).add(1);
		} else if constexpr (A == Arm::stripe_plain) {
			auto word = at(stripe_lines[stripe].cell[cell]);
			word.store(word.load() + 1);
		} else if constexpr (A == Arm::knob_sampled) {
			auto countdown = at(stripe_lines[stripe].cell[7]);
			if (const auto left = countdown.load()) [[likely]] {
				countdown.store(left - 1);
			} else {
				countdown.store(63);
				sample(name, cell);
			}
		} else {
			const auto key = A == Arm::knob_ptr_rows8 ? std::uint64_t(std::uintptr_t(name.text.data()))
			                                          : name_hash(name.text.data(), name.text.size());
			const auto slot = table->find(key);
			auto &cells = A == Arm::knob_rows64 ? rows64 : A == Arm::knob_rows8_mapped ? rows8_mapped : rows8;
			const auto row = A == Arm::knob_rows64 ? stripe : stripe & 7;
			at(cells.line(row, slot)[cell]).add(1);
		}
	} else if constexpr (A == Arm::tls) {
		++thread_count;
	} else if constexpr (A == Arm::knob_probe) {
		const auto slot = table->find(name_hash(name.text.data(), name.text.size()));
		escape(slot);
	} else if constexpr (A == Arm::knob_shared) {
		const auto slot = table->find(name_hash(name.text.data(), name.text.size()));
		at(shared_cells.line(0, slot)[cell]).add(1);
	} else if constexpr (A == Arm::pos_shared) {
		pos_counts[name.pos].fetch_add(1, relaxed);
	} else if constexpr (A == Arm::pos_stripe_rmw) {
		const char frame = 0;
		(void)__atomic_fetch_add(&stripe_counts[stripe_of(&frame) * positions + name.pos], 1u, __ATOMIC_RELAXED);
	} else if constexpr (A == Arm::pos_tls) {
		auto *row = tls_counts;
		if (!row) [[unlikely]]
			row = tls_allocate();
		__atomic_store_n(&row[name.pos], __atomic_load_n(&row[name.pos], __ATOMIC_RELAXED) + 1, __ATOMIC_RELAXED);
	} else if constexpr (A == Arm::ever_bit) {
		auto &word = ever_bits[name.pos >> 6];
		const auto bit = std::uint64_t(1) << (name.pos & 63);
		if (!(word.load(relaxed) & bit)) [[unlikely]]
			word.fetch_or(bit, relaxed);
	} else if constexpr (A == Arm::ever_sampled) {
		count<Arm::ever_bit>(name, status, value);
		count<Arm::knob_sampled>(name, status, value);
	} else if constexpr (A == Arm::knob_tls) {
		auto countdown = at(tls_countdown);
		if (const auto left = countdown.load()) [[likely]] {
			countdown.store(left - 1);
		} else {
			countdown.store(63);
			sample(name, cell);
		}
	} else if constexpr (A == Arm::es_tls) {
		count<Arm::ever_bit>(name, status, value);
		count<Arm::knob_tls>(name, status, value);
	} else if constexpr (A == Arm::es_one || A == Arm::es_tls_one) {
		std::uint64_t *countdown;
		if constexpr (A == Arm::es_one) {
			const char frame = 0;
			countdown = &stripe_lines[stripe_of(&frame)].cell[7];
		} else {
			countdown = &tls_countdown;
		}
		const auto set = ever_bits[name.pos >> 6].load(relaxed) >> (name.pos & 63) & 1;
		const auto left = at(*countdown).load();
		if (set & std::uint64_t(left != 0)) [[likely]]
			at(*countdown).store(left - 1);
		else
			ever_slow(name, status, value, countdown, left);
	} else if constexpr (A == Arm::dm_cache) {
		auto &line = dm_line;
		const auto i = name.pos & 7;
		if (line.key[i] == name.pos + 1) {
			++line.count[i];
		} else {
			if (line.count[i])
				pos_counts[line.key[i] - 1].fetch_add(line.count[i], relaxed);
			line.key[i] = name.pos + 1;
			line.count[i] = 1;
		}
	} else if constexpr (A == Arm::hook_off || A == Arm::hook_on) {
		if (const auto h = hook.load(relaxed)) {
			const Event event{name.text.data(), name.text.size(), int(status), int(cell), value};
			h(nullptr, &event);
		}
	}
}

struct Options {
	std::string store, map_dir, arms = "all", threads = "1,8";
	std::size_t knobs = 100000, reps = 5, calls = 262144, scattered = 1024, soak = 0;
	bool interleave = false, bare = false;
};
Options parse(int argc, char **argv) {
	Options o;
	for (int i = 1; i + 1 < argc; i += 2) {
		const std::string flag = argv[i], value = argv[i + 1];
		if (flag == "--store") o.store = value;
		else if (flag == "--map-dir") o.map_dir = value;
		else if (flag == "--arms") o.arms = value;
		else if (flag == "--threads") o.threads = value;
		else if (flag == "--knobs") o.knobs = std::stoull(value);
		else if (flag == "--reps") o.reps = std::stoull(value);
		else if (flag == "--calls") o.calls = std::stoull(value);
		else if (flag == "--scattered") o.scattered = std::stoull(value);
		else if (flag == "--soak") o.soak = std::stoull(value);
		else if (flag == "--interleave") o.interleave = value == "1";
		else if (flag == "--bare") o.bare = value == "1";
		else throw std::invalid_argument("unknown flag " + flag);
	}
	if (o.store.empty() || o.map_dir.empty())
		throw std::invalid_argument("--store and --map-dir are required");
	if (o.knobs < 2)
		throw std::invalid_argument("--knobs must be at least 2");
	o.calls = std::max<std::size_t>(2 * batch, o.calls / (2 * batch) * (2 * batch));
	return o;
}
std::vector<std::string> split_list(const std::string &list) {
	std::vector<std::string> out;
	std::stringstream in(list);
	for (std::string item; std::getline(in, item, ',');)
		out.push_back(item);
	return out;
}
// Names of realistic length (about 40 bytes): 64 namespaces, a plain and a rollout knob per index;
// `--knobs` counts definitions, so there are knobs / 2 indices.
std::string knob_name(std::size_t i, bool rollout) {
	return "svc_namespace_" + std::to_string(i % 64) + ":feature_rollout_knob_" + std::to_string(i) +
	       (rollout ? "_enabled" : "_limit");
}
// Builds the definitions and checks them against the parser's limits before publishing, so a set the
// store rejects fails with its reason before any case is timed.
void generate(const Options &o) {
	std::string json = R"({"schema_version":1,"knobs":{)";
	for (std::size_t i = 0; i != o.knobs / 2; ++i)
		json += (i ? ",\"" : "\"") + knob_name(i, false) + R"(":{"default":)" + std::to_string(i + 1) + "},\"" +
		        knob_name(i, true) + R"(":{"default":0,"rollout_pct":50,"rollout_value":1})";
	json += "}}";
	const auto parsed = detent::parse_defaults(json, "observability_bench");
	if (!parsed)
		throw std::invalid_argument("knob definitions rejected (" + std::to_string(o.knobs) + " knobs, " +
		                            std::to_string(json.size()) + " bytes; limits " +
		                            std::to_string(detent::kDefaultMaxKnobs) + " knobs, " +
		                            std::to_string(detent::kDefaultMaxBytes) + " bytes): " + parsed.error().detail);
	if (parsed.value().knobs.size() != o.knobs / 2 * 2)
		throw std::invalid_argument("parsed " + std::to_string(parsed.value().knobs.size()) + " knobs, built " +
		                            std::to_string(o.knobs / 2 * 2));
	detent::testing::GenerationWriter(o.store).publish_json(json);
}

struct Result {
	std::vector<float> per_call; // per-call mean of each batch, ns (interleaved: of each `none` block)
	std::vector<float> diff;     // interleaved: per-call (arm - none) of each block pair, ns
	std::uint64_t stripe = 0;
};
using Lookup = detent_status (*)(detent_reader *, const Name &, std::int64_t &);
detent_status lookup_get(detent_reader *core, const Name &name, std::int64_t &value) {
	return detent_get(core, name.str(), 0, &value);
}
template <Arm A>
void timed(detent_reader *core, Lookup lookup, const std::vector<Name> &names, std::size_t calls, Result &result) {
	const char frame = 0;
	result.stripe = stripe_of(&frame);
	result.per_call.assign(calls / batch, 0.0f);
	thread_count = 0;
	counting = true;
	std::size_t i = 0;
	for (auto &mean : result.per_call) {
		const auto start = Clock::now();
		for (std::size_t j = 0; j != batch; ++j) {
			const auto &name = names[i];
			i = i + 1 == names.size() ? 0 : i + 1;
			std::int64_t value = 0;
			const auto status = lookup(core, name, value);
			count<A>(name, status, value);
			escape(value);
		}
		mean = float(std::chrono::duration<double, std::nano>(Clock::now() - start).count() / batch);
	}
	counting = false;
	thread_total.fetch_add(thread_count, relaxed);
}
using Timed = void (*)(detent_reader *, Lookup, const std::vector<Name> &, std::size_t, Result &);
constexpr Timed timed_arms[] = {
#define X(a) &timed<Arm::a>,
    ARMS(X)
#undef X
};
detent_status lookup_bare(detent_reader *, const Name &, std::int64_t &value) {
	value = 1;
	return DETENT_OK;
}
inline bool on_first(std::size_t pair, std::size_t cycle) noexcept { return ((pair ^ (pair / cycle)) & 1) != 0; }
// One block of 64 calls of arm A; returns its per-call mean.
template <Arm A>
float block(detent_reader *core, Lookup lookup, const std::vector<Name> &names, std::size_t &i) noexcept {
	const auto start = Clock::now();
	for (std::size_t j = 0; j != batch; ++j) {
		const auto &name = names[i];
		i = i + 1 == names.size() ? 0 : i + 1;
		std::int64_t value = 0;
		const auto status = lookup(core, name, value);
		count<A>(name, status, value);
		escape(value);
	}
	return float(std::chrono::duration<double, std::nano>(Clock::now() - start).count() / batch);
}
// OB1's paired method: block pairs of the arm and `none` on one thread over the same 64 names, the order
// swapped every pair and every cycle of the name set, so each name sees both orders.
template <Arm A>
void interleaved(detent_reader *core, Lookup lookup, const std::vector<Name> &names, std::size_t calls,
                 Result &result) {
	const char frame = 0;
	result.stripe = stripe_of(&frame);
	const auto pairs = calls / (2 * batch);
	result.per_call.assign(pairs, 0.0f);
	result.diff.assign(pairs, 0.0f);
	counting = true;
	std::size_t i = 0;
	const auto cycle = std::max<std::size_t>(2, names.size() / batch);
	for (std::size_t k = 0; k != pairs; ++k) {
		float on, off;
		const auto first = i;
		if (on_first(k, cycle)) {
			on = block<A>(core, lookup, names, i);
			i = first;
			off = block<Arm::none>(core, lookup, names, i);
		} else {
			off = block<Arm::none>(core, lookup, names, i);
			i = first;
			on = block<A>(core, lookup, names, i);
		}
		result.per_call[k] = off;
		result.diff[k] = on - off;
	}
	counting = false;
	if constexpr (A == Arm::dm_cache)
		dm_flush();
}
constexpr Timed interleaved_arms[] = {
#define X(a) &interleaved<Arm::a>,
    ARMS(X)
#undef X
};
// Clears A8's position rows only for paired cases, so the other cases keep their cache state.
void reset_counters(bool positions_too = false) {
	shared_counter.store(0, relaxed);
	std::destroy_at(&metrics_counter);
	std::construct_at(&metrics_counter);
	std::destroy_at(&contended_counter);
	std::construct_at(&contended_counter);
	std::memset(stripe_lines, 0, sizeof stripe_lines);
	thread_total.store(0, relaxed);
	table->clear();
	for (auto *cells : {&shared_cells, &rows8, &rows64, &rows8_mapped})
		cells->clear();
	if (!positions_too)
		return;
	for (std::size_t p = 0; p != positions; ++p)
		pos_counts[p].store(0, relaxed);
	std::memset(stripe_counts, 0, stripes * positions * sizeof(std::uint32_t));
	for (std::size_t w = 0; w != positions / 64 + 1; ++w)
		ever_bits[w].store(0, relaxed);
	for (auto *row : tls_rows)
		std::free(row);
	tls_rows.clear();
	tls_counts = nullptr; // this thread's; the workers that held the others have exited
	tls_allocations.store(0, relaxed);
	dm_line = {};
	tls_countdown = 63;
}
// Counts the arm kept, or -1 for an arm that keeps none.
std::int64_t counted(Arm arm) {
	std::uint64_t sum = 0;
	switch (arm) {
	case Arm::shared: return std::int64_t(shared_counter.load());
	case Arm::metrics_atomic: return std::int64_t(metrics_counter.get());
	case Arm::metrics_contended: return std::int64_t(contended_counter.get());
	case Arm::stripe_rmw:
	case Arm::stripe_plain:
		for (auto &line : stripe_lines)
			for (std::size_t c = 0; c != 7; ++c)
				sum += line.cell[c];
		return std::int64_t(sum);
	case Arm::tls: return std::int64_t(thread_total.load());
	case Arm::knob_shared:
	case Arm::knob_sampled:
	case Arm::knob_tls: return std::int64_t(shared_cells.total());
	case Arm::knob_rows8:
	case Arm::knob_ptr_rows8: return std::int64_t(rows8.total());
	case Arm::knob_rows64: return std::int64_t(rows64.total());
	case Arm::knob_rows8_mapped: return std::int64_t(rows8_mapped.total());
	case Arm::pos_shared:
	case Arm::dm_cache:
		for (std::size_t p = 0; p != positions; ++p)
			sum += pos_counts[p].load(relaxed);
		return std::int64_t(sum);
	case Arm::pos_stripe_rmw:
		for (std::size_t c = 0; c != stripes * positions; ++c)
			sum += __atomic_load_n(&stripe_counts[c], __ATOMIC_RELAXED);
		return std::int64_t(sum);
	case Arm::pos_tls:
		for (const auto *row : tls_rows)
			for (std::size_t p = 0; p != positions; ++p)
				sum += row[p];
		return std::int64_t(sum);
	case Arm::ever_bit:
	case Arm::ever_sampled:
	case Arm::es_tls:
	case Arm::es_one:
	case Arm::es_tls_one: // distinct definitions seen
		for (std::size_t w = 0; w != positions / 64 + 1; ++w)
			sum += std::uint64_t(__builtin_popcountll(ever_bits[w].load(relaxed)));
		return std::int64_t(sum);
	default: return -1;
	}
}
std::uint64_t minor_faults() {
	rusage usage{};
	::getrusage(RUSAGE_SELF, &usage);
	return std::uint64_t(usage.ru_minflt);
}
float percentile(std::vector<float> v, double p) {
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, std::size_t(double(v.size()) * p))];
}

void run_case(detent_reader *core, const Options &o, Arm arm, const std::string &set,
              const std::vector<Name> &names, std::size_t threads, std::size_t rep) {
	const auto lookup = &lookup_get;
	reset_counters();
	hook.store(arm == Arm::hook_on ? &noop_hook : nullptr);
	// Untimed warmup on this thread: attaches, verifies pages and claims each name's table slot.
	{
		Result warm;
		timed_arms[std::size_t(arm)](core, lookup, names, std::max<std::size_t>(batch, names.size() / batch * batch + batch),
		                             warm);
	}
	reset_counters();
	std::vector<Result> results(threads);
	std::atomic<std::size_t> ready{0};
	allocations = 0;
	const auto faults = minor_faults();
	std::vector<std::thread> workers;
	for (std::size_t t = 0; t != threads; ++t)
		workers.emplace_back([&, t] {
			ready.fetch_add(1);
			while (ready.load() != threads) {
			}
			timed_arms[std::size_t(arm)](core, lookup, names, o.calls, results[t]);
		});
	for (auto &worker : workers)
		worker.join();
	const auto fault_delta = minor_faults() - faults;
	std::vector<float> p50, p99;
	std::set<std::uint64_t> distinct, distinct8;
	for (const auto &r : results) {
		p50.push_back(percentile(r.per_call, 0.50));
		p99.push_back(percentile(r.per_call, 0.99));
		distinct.insert(r.stripe);
		distinct8.insert(r.stripe & 7);
	}
	std::cout << "{\"arm\":\"" << arm_names[std::size_t(arm)] << "\",\"op\":\"get\",\"set\":\"" << set
	          << "\",\"threads\":" << threads << ",\"rep\":" << rep << ",\"p50_ns\":" << percentile(p50, 0.5)
	          << ",\"p50_max_ns\":" << *std::max_element(p50.begin(), p50.end())
	          << ",\"p99_ns\":" << percentile(p99, 0.5) << ",\"calls\":" << o.calls * threads
	          << ",\"counted\":" << counted(arm) << ",\"allocations\":" << allocations.load()
	          << ",\"minor_faults\":" << fault_delta << ",\"stripes\":" << distinct.size()
	          << ",\"rows8\":" << distinct8.size() << "}" << std::endl;
}
// OB1's paired case: every thread warms its own counters, then alternates arm and `none` blocks.
void run_pair(detent_reader *core, const Options &o, Arm arm, const std::string &set,
              const std::vector<Name> &names, std::size_t threads, std::size_t rep) {
	const auto lookup = o.bare ? &lookup_bare : &lookup_get;
	reset_counters(true);
	hook.store(arm == Arm::hook_on ? &noop_hook : nullptr);
	const auto warm_calls = names.size() / batch * batch + batch;
	{
		Result warm;
		timed_arms[std::size_t(arm)](core, lookup, names, warm_calls, warm);
		if (arm == Arm::dm_cache)
			dm_flush();
	}
	reset_counters(true);
	std::vector<Result> results(threads);
	std::atomic<std::size_t> ready{0};
	allocations = 0;
	const auto faults = minor_faults();
	std::vector<std::thread> workers;
	for (std::size_t t = 0; t != threads; ++t)
		workers.emplace_back([&, t] {
			Result warm; // this thread's own rows, lines and countdown, untimed
			timed_arms[std::size_t(arm)](core, lookup, names, warm_calls, warm);
			if (arm == Arm::dm_cache)
				dm_flush();
			ready.fetch_add(1);
			while (ready.load() != threads) {
			}
			interleaved_arms[std::size_t(arm)](core, lookup, names, o.calls, results[t]);
		});
	for (auto &worker : workers)
		worker.join();
	const auto fault_delta = minor_faults() - faults;
	// The statistic: the mean of the two orders' medians, so warming the second block's names cancels.
	const auto cycle = std::max<std::size_t>(2, names.size() / batch);
	std::vector<float> diffs, on_first_diffs, off_first_diffs, offs, thread_medians;
	std::set<std::uint64_t> distinct;
	for (const auto &r : results) {
		std::vector<float> mine[2];
		for (std::size_t k = 0; k != r.diff.size(); ++k)
			mine[on_first(k, cycle)].push_back(r.diff[k]);
		off_first_diffs.insert(off_first_diffs.end(), mine[0].begin(), mine[0].end());
		on_first_diffs.insert(on_first_diffs.end(), mine[1].begin(), mine[1].end());
		diffs.insert(diffs.end(), r.diff.begin(), r.diff.end());
		offs.insert(offs.end(), r.per_call.begin(), r.per_call.end());
		thread_medians.push_back((percentile(mine[0], 0.5) + percentile(mine[1], 0.5)) / 2);
		distinct.insert(r.stripe);
	}
	const auto on_first_median = percentile(on_first_diffs, 0.5), off_first_median = percentile(off_first_diffs, 0.5);
	std::set<std::uint32_t> seen;
	for (const auto &name : names)
		seen.insert(name.pos);
	const auto on_calls = std::int64_t((warm_calls + o.calls / 2) * threads);
	std::int64_t expected = -1;
	switch (arm) {
	case Arm::pos_shared:
	case Arm::pos_stripe_rmw:
	case Arm::pos_tls:
	case Arm::dm_cache: expected = on_calls; break;
	case Arm::ever_bit:
	case Arm::ever_sampled:
	case Arm::es_tls:
	case Arm::es_one:
	case Arm::es_tls_one: expected = std::int64_t(seen.size()); break;
	default: break;
	}
	std::cout << "{\"pair\":\"" << arm_names[std::size_t(arm)] << "\",\"op\":\"get\",\"set\":\"" << set
	          << "\",\"bare\":" << o.bare << ",\"threads\":" << threads << ",\"rep\":" << rep
	          << ",\"delta_ns\":" << (on_first_median + off_first_median) / 2
	          << ",\"order_ns\":" << (off_first_median - on_first_median) / 2
	          << ",\"pooled_ns\":" << percentile(diffs, 0.5) << ",\"delta_q25_ns\":" << percentile(diffs, 0.25)
	          << ",\"delta_q75_ns\":" << percentile(diffs, 0.75)
	          << ",\"thread_min_ns\":" << *std::min_element(thread_medians.begin(), thread_medians.end())
	          << ",\"thread_max_ns\":" << *std::max_element(thread_medians.begin(), thread_medians.end())
	          << ",\"off_p50_ns\":" << percentile(offs, 0.5) << ",\"pairs\":" << diffs.size()
	          << ",\"counted\":" << counted(arm) << ",\"expected\":" << expected
	          << ",\"allocations\":" << allocations.load() << ",\"thread_rows\":" << tls_allocations.load()
	          << ",\"minor_faults\":" << fault_delta << ",\"stripes\":" << distinct.size() << "}" << std::endl;
}
// One thread for `seconds` on a counting arm: minor faults and the slowest batch, per second.
void soak(detent_reader *core, const Options &o, Arm arm, const std::vector<Name> &names) {
	reset_counters();
	Result warm;
	timed_arms[std::size_t(arm)](core, &lookup_get, names, names.size() / batch * batch + batch, warm);
	std::uint64_t total_faults = 0;
	float worst = 0;
	std::vector<float> seconds_p50;
	for (std::size_t s = 0; s != o.soak; ++s) {
		const auto faults = minor_faults();
		const auto end = Clock::now() + std::chrono::seconds(1);
		std::vector<float> means;
		while (Clock::now() < end) {
			Result r;
			timed_arms[std::size_t(arm)](core, &lookup_get, names, batch * 256, r);
			for (float m : r.per_call)
				means.push_back(m), worst = std::max(worst, m);
		}
		total_faults += minor_faults() - faults;
		seconds_p50.push_back(percentile(means, 0.5));
	}
	std::cout << "{\"soak\":\"" << arm_names[std::size_t(arm)] << "\",\"seconds\":" << o.soak
	          << ",\"p50_ns\":" << percentile(seconds_p50, 0.5) << ",\"worst_batch_ns\":" << worst
	          << ",\"minor_faults\":" << total_faults << ",\"counted\":" << counted(arm) << "}" << std::endl;
}
} // namespace

int main(int argc, char **argv) try {
	const auto o = parse(argc, argv);
	const auto started = Clock::now();
	if (!std::filesystem::exists(std::filesystem::path(o.store) / "change"))
		generate(o);
	detent_reader *core = nullptr;
	const detent_open_options options{sizeof options, {o.store.data(), o.store.size()}, nullptr, 0,
	                                  std::uint32_t(::getuid())};
	if (detent_open(&options, &core) != DETENT_OK)
		throw std::runtime_error("detent_open failed");
	// Name sets: one hot name and `scattered` seeded names per operation.
	std::mt19937_64 random(0x4f4253455256ull);
	const auto indices = o.knobs / 2;
	const auto named = [](std::size_t k, bool rollout) { return Name{knob_name(k, rollout), std::uint32_t(2 * k + rollout)}; };
	std::vector<Name> hot{named(indices / 2, false)}, scattered;
	for (std::size_t i = 0; i != o.scattered; ++i) {
		const auto draw = random();
		scattered.push_back(named(draw / 2 % indices, draw & 1));
	}
	positions = indices * 2;
	pos_counts = new std::atomic<std::uint64_t>[positions]();
	stripe_counts = static_cast<std::uint32_t *>(std::calloc(stripes * positions, sizeof(std::uint32_t)));
	ever_bits = new std::atomic<std::uint64_t>[positions / 64 + 1]();
	tls_rows.reserve(1024); // a registry push never allocates inside a timed loop
	// The table holds twice the scattered set; each row has a line per slot.
	std::size_t capacity = 1;
	while (capacity < 2 * o.scattered)
		capacity <<= 1;
	Table names_table(capacity);
	table = &names_table;
	shared_cells.init(1, capacity, "");
	rows8.init(8, capacity, "");
	rows64.init(stripes, capacity, "");
	rows8_mapped.init(8, capacity, o.map_dir + "/usage-" + std::to_string(::getpid()) + ".cells");
	// Smoke assertions: every name answers OK with its default (a hashless read of a rollout knob too).
	for (const auto *set : {&hot, &scattered})
		for (const auto &name : *set) {
			std::int64_t value = -1;
			const auto expected = name.pos & 1 ? 0 : std::int64_t(name.pos / 2 + 1);
			if (detent_get(core, name.str(), -1, &value) != DETENT_OK || value != expected)
				throw std::runtime_error("knob does not answer its default: " + name.text);
		}
	std::vector<Arm> arms;
	if (o.arms == "all") // D2's arms; A8's run by name with --interleave 1
		for (std::size_t a = 0; a <= std::size_t(Arm::hook_on); ++a)
			arms.push_back(Arm(a));
	else
		for (const auto &item : split_list(o.arms)) {
			const auto found = std::find_if(std::begin(arm_names), std::end(arm_names),
			                                [&](const char *n) { return item == n; });
			if (found == std::end(arm_names))
				throw std::invalid_argument("unknown arm " + item);
			arms.push_back(Arm(found - std::begin(arm_names)));
		}
	std::cout << "{\"config\":{\"knobs\":" << indices * 2 << ",\"scattered\":" << o.scattered
	          << ",\"table_capacity\":" << capacity << ",\"calls_per_thread\":" << o.calls << ",\"reps\":" << o.reps
	          << ",\"threads\":\"" << o.threads << "\",\"arms\":" << arms.size()
	          << ",\"hot_name_bytes\":" << hot.front().text.size()
	          << ",\"rows64_bytes\":" << rows64.bytes << ",\"rows8_bytes\":" << rows8.bytes
	          << ",\"interleave\":" << o.interleave << ",\"bare\":" << o.bare << ",\"positions\":" << positions
	          << ",\"mapped\":" << rows8_mapped.mapped << ",\"hardware_threads\":" << std::thread::hardware_concurrency()
	          << ",\"setup_s\":" << std::chrono::duration<double>(Clock::now() - started).count() << "}}" << std::endl;
	const auto thread_counts = split_list(o.threads);
	for (std::size_t rep = 0; rep != o.reps; ++rep)
		for (std::size_t a = 0; a != arms.size(); ++a) {
			const auto arm = arms[(a + rep) % arms.size()]; // rotate the order every repetition
			for (const auto &t : thread_counts) {
				const auto run = o.interleave ? &run_pair : &run_case;
				run(core, o, arm, "hot", hot, std::stoull(t), rep);
				run(core, o, arm, "scattered", scattered, std::stoull(t), rep);
			}
		}
	if (o.soak)
		for (const auto arm : {Arm::knob_rows8, Arm::knob_rows8_mapped})
			soak(core, o, arm, scattered);
	std::cout << "{\"elapsed_s\":" << std::chrono::duration<double>(Clock::now() - started).count() << "}" << std::endl;
	detent_close(core);
	return 0;
} catch (const std::exception &error) {
	std::cerr << "observability_bench: " << error.what() << std::endl;
	return 1;
}
