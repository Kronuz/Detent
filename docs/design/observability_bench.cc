// observability_bench: the warm-lookup cost of each knob-usage counting option (docs/design/observability.md
// D2), measured around reader S6's real lookup. observability-bench.sh builds it against `Detent:b1d281c`.
//
//   observability_bench --store DIR [--knobs N] [--reps N] [--calls N] [--threads 1,8] [--ops int,bool]
//                       [--arms a,b,...] [--scattered N] [--map-dir DIR] [--soak SECONDS]
//
// Each case prints one JSON line. A case times batches of 64 calls with steady_clock and reports the
// per-call mean of each batch, so the clock costs under 1 ns per call. The counting code runs after
// the lookup in this file, not inside the core: the core already holds the stripe this file
// recomputes, so a striped arm here costs about one hash more than it would inside the core.
#include "detent/reader.h"
#include "detent_testing/generation_writer.hh"
#include <metrics.hh>
#include <algorithm>
#include <atomic>
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

#define ARMS(X)                                                                                                       \
	X(none) X(none2) X(shared) X(metrics_atomic) X(metrics_contended) X(stripe_rmw) X(stripe_plain) X(tls) X(knob_probe) X(knob_shared)     \
	    X(knob_rows8) X(knob_rows64) X(knob_ptr_rows8) X(knob_sampled) X(knob_rows8_mapped) X(hook_off) X(hook_on)
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

struct Name {
	std::string text;
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

template <Arm A> inline void count(const Name &name, detent_status status, std::int64_t value) noexcept {
	const unsigned cell = status != DETENT_OK ? 6u : (value != 0 ? 1u : 0u);
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
			if (const auto left = countdown.load()) {
				countdown.store(left - 1);
			} else {
				countdown.store(63);
				const auto slot = table->find(name_hash(name.text.data(), name.text.size()));
				at(shared_cells.line(0, slot)[cell]).add(64);
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
	} else if constexpr (A == Arm::hook_off || A == Arm::hook_on) {
		if (const auto h = hook.load(relaxed)) {
			const Event event{name.text.data(), name.text.size(), int(status), int(cell), value};
			h(nullptr, &event);
		}
	}
}

struct Options {
	std::string store, map_dir, arms = "all", threads = "1,8", ops = "int,bool";
	std::size_t knobs = 100000, reps = 5, calls = 262144, scattered = 1024, soak = 0;
};
Options parse(int argc, char **argv) {
	Options o;
	for (int i = 1; i + 1 < argc; i += 2) {
		const std::string flag = argv[i], value = argv[i + 1];
		if (flag == "--store") o.store = value;
		else if (flag == "--map-dir") o.map_dir = value;
		else if (flag == "--arms") o.arms = value;
		else if (flag == "--threads") o.threads = value;
		else if (flag == "--ops") o.ops = value;
		else if (flag == "--knobs") o.knobs = std::stoull(value);
		else if (flag == "--reps") o.reps = std::stoull(value);
		else if (flag == "--calls") o.calls = std::stoull(value);
		else if (flag == "--scattered") o.scattered = std::stoull(value);
		else if (flag == "--soak") o.soak = std::stoull(value);
		else throw std::invalid_argument("unknown flag " + flag);
	}
	if (o.store.empty() || o.map_dir.empty())
		throw std::invalid_argument("--store and --map-dir are required");
	o.calls = std::max<std::size_t>(batch, o.calls / batch * batch);
	return o;
}
std::vector<std::string> split_list(const std::string &list) {
	std::vector<std::string> out;
	std::stringstream in(list);
	for (std::string item; std::getline(in, item, ',');)
		out.push_back(item);
	return out;
}
// Names of realistic length (about 40 bytes): 64 namespaces, an int and a bool knob per index.
std::string knob_name(std::size_t i, bool boolean) {
	return "svc_namespace_" + std::to_string(i % 64) + ":feature_rollout_knob_" + std::to_string(i) +
	       (boolean ? "_enabled" : "_limit");
}
void generate(const Options &o) {
	detent::testing::GenerationWriter writer(o.store);
	std::string json = R"({"schema_version":1,"knobs":{)";
	for (std::size_t i = 0; i != o.knobs; ++i)
		json += (i ? ",\"" : "\"") + knob_name(i, false) + R"(":{"type":"int","default":)" + std::to_string(i + 1) +
		        "},\"" + knob_name(i, true) + R"(":{"type":"bool","default":false,"rollout_pct":50})";
	json += "}}";
	writer.publish_json(json);
}

struct Result {
	std::vector<float> per_call; // per-call mean of each batch, ns
	std::uint64_t stripe = 0;
};
using Lookup = detent_status (*)(detent_reader *, const Name &, std::int64_t &);
detent_status lookup_int(detent_reader *core, const Name &name, std::int64_t &value) {
	return detent_get(core, name.str(), 0, &value);
}
detent_status lookup_bool(detent_reader *core, const Name &name, std::int64_t &value) {
	bool out = false;
	const auto status = detent_eval(core, name.str(), {}, {}, false, &out);
	value = out;
	return status;
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
void reset_counters() {
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
	case Arm::knob_sampled: return std::int64_t(shared_cells.total());
	case Arm::knob_rows8:
	case Arm::knob_ptr_rows8: return std::int64_t(rows8.total());
	case Arm::knob_rows64: return std::int64_t(rows64.total());
	case Arm::knob_rows8_mapped: return std::int64_t(rows8_mapped.total());
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

void run_case(detent_reader *core, const Options &o, Arm arm, const std::string &op, const std::string &set,
              const std::vector<Name> &names, std::size_t threads, std::size_t rep) {
	const auto lookup = op == "int" ? &lookup_int : &lookup_bool;
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
	std::cout << "{\"arm\":\"" << arm_names[std::size_t(arm)] << "\",\"op\":\"" << op << "\",\"set\":\"" << set
	          << "\",\"threads\":" << threads << ",\"rep\":" << rep << ",\"p50_ns\":" << percentile(p50, 0.5)
	          << ",\"p50_max_ns\":" << *std::max_element(p50.begin(), p50.end())
	          << ",\"p99_ns\":" << percentile(p99, 0.5) << ",\"calls\":" << o.calls * threads
	          << ",\"counted\":" << counted(arm) << ",\"allocations\":" << allocations.load()
	          << ",\"minor_faults\":" << fault_delta << ",\"stripes\":" << distinct.size()
	          << ",\"rows8\":" << distinct8.size() << "}" << std::endl;
}
// One thread for `seconds` on a counting arm: minor faults and the slowest batch, per second.
void soak(detent_reader *core, const Options &o, Arm arm, const std::vector<Name> &names) {
	reset_counters();
	Result warm;
	timed_arms[std::size_t(arm)](core, &lookup_int, names, names.size() / batch * batch + batch, warm);
	std::uint64_t total_faults = 0;
	float worst = 0;
	std::vector<float> seconds_p50;
	for (std::size_t s = 0; s != o.soak; ++s) {
		const auto faults = minor_faults();
		const auto end = Clock::now() + std::chrono::seconds(1);
		std::vector<float> means;
		while (Clock::now() < end) {
			Result r;
			timed_arms[std::size_t(arm)](core, &lookup_int, names, batch * 256, r);
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
	std::vector<Name> hot_int{{knob_name(o.knobs / 2, false)}}, hot_bool{{knob_name(o.knobs / 2, true)}}, sc_int,
	    sc_bool;
	for (std::size_t i = 0; i != o.scattered; ++i) {
		const auto k = random() % o.knobs;
		sc_int.push_back({knob_name(k, false)});
		sc_bool.push_back({knob_name(k, true)});
	}
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
	// Smoke assertions: every name answers OK with the value it was published with.
	for (const auto *set : {&hot_int, &sc_int}) {
		for (const auto &name : *set) {
			std::int64_t value = 0;
			if (detent_get(core, name.str(), -1, &value) != DETENT_OK || value <= 0)
				throw std::runtime_error("int knob does not answer: " + name.text);
		}
	}
	for (const auto *set : {&hot_bool, &sc_bool})
		for (const auto &name : *set) {
			bool value = true;
			if (detent_eval(core, name.str(), {}, {}, true, &value) != DETENT_OK || value)
				throw std::runtime_error("bool knob does not answer its default: " + name.text);
		}
	std::vector<Arm> arms;
	if (o.arms == "all")
		for (std::size_t a = 0; a != arm_count; ++a)
			arms.push_back(Arm(a));
	else
		for (const auto &item : split_list(o.arms)) {
			const auto found = std::find_if(std::begin(arm_names), std::end(arm_names),
			                                [&](const char *n) { return item == n; });
			if (found == std::end(arm_names))
				throw std::invalid_argument("unknown arm " + item);
			arms.push_back(Arm(found - std::begin(arm_names)));
		}
	std::cout << "{\"config\":{\"knobs\":" << 2 * o.knobs << ",\"scattered\":" << o.scattered
	          << ",\"table_capacity\":" << capacity << ",\"calls_per_thread\":" << o.calls << ",\"reps\":" << o.reps
	          << ",\"threads\":\"" << o.threads << "\",\"ops\":\"" << o.ops << "\",\"arms\":" << arms.size()
	          << ",\"hot_name_bytes\":" << hot_int.front().text.size()
	          << ",\"rows64_bytes\":" << rows64.bytes << ",\"rows8_bytes\":" << rows8.bytes
	          << ",\"mapped\":" << rows8_mapped.mapped << ",\"hardware_threads\":" << std::thread::hardware_concurrency()
	          << ",\"setup_s\":" << std::chrono::duration<double>(Clock::now() - started).count() << "}}" << std::endl;
	const auto thread_counts = split_list(o.threads);
	for (std::size_t rep = 0; rep != o.reps; ++rep)
		for (std::size_t a = 0; a != arms.size(); ++a) {
			const auto arm = arms[(a + rep) % arms.size()]; // rotate the order every repetition
			for (const auto &op : split_list(o.ops))
				for (const auto &t : thread_counts) {
					run_case(core, o, arm, op, "hot", op == "int" ? hot_int : hot_bool, std::stoull(t), rep);
					run_case(core, o, arm, op, "scattered", op == "int" ? sc_int : sc_bool, std::stoull(t), rep);
				}
		}
	if (o.soak)
		for (const auto arm : {Arm::knob_rows8, Arm::knob_rows8_mapped})
			soak(core, o, arm, sc_int);
	std::cout << "{\"elapsed_s\":" << std::chrono::duration<double>(Clock::now() - started).count() << "}" << std::endl;
	detent_close(core);
	return 0;
} catch (const std::exception &error) {
	std::cerr << "observability_bench: " << error.what() << std::endl;
	return 1;
}
