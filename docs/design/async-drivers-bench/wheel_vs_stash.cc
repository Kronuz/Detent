// Kronuz/wheel's timer_wheel (the library, fetched) and stash on one workload, in one build:
//   (b)  stash as it is (stash.h), Xapiand's four levels, entry* values; cancel clears a flag
//        (b2 runs b again: the A/A twin)
//   (w)  wheel::timer_wheel (wheel.hh): intrusive 32-byte entries, any thread arms with arm_remote
// Modes, one line of results each:
//   single ARM N        arm, re-arm, cancel and expire on one thread, on a manual clock
//   cross ARM P TOTAL   P producer threads arm TOTAL entries (default 1,000,000), one owner
//                       fires on steady_clock
//   mem ARM N           (the COUNT_HEAP build) bytes per entry and heap per arm
//   test                both arms against a reference model on a manual clock, and
//                       cross-thread runs checked for exactly-once, never-early fires
// ARM is b, b2 or w.
#ifdef COUNT_HEAP
#include "probe.hh"
PROBE_ALLOCATOR
#endif

#include <wheel.hh>  // the fetched library: angle brackets, so no copy beside this file can shadow it
#include "stash.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using u64 = std::uint64_t;
constexpr u64 ms = 1'000'000;
constexpr u64 manual_base = 7'000 * ms;  // far below steady_clock: a wheel reading it would fire early

#define CHECK(c)                                                                     \
	do {                                                                             \
		if (!(c)) {                                                                  \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
			std::exit(1);                                                            \
		}                                                                            \
	} while (0)

u64 now_ns() {
	return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::string utc() {
	auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	char b[32];
	std::strftime(b, sizeof b, "%FT%TZ", std::gmtime(&t));
	return b;
}

// The core clock in GHz: 8 dependent one-cycle adds per iteration, 4 ms or so.
double core_ghz() {
	constexpr u64 iterations = 1'000'000;
	u64 x = 0;
	const u64 t0 = now_ns();
	for (u64 i = 0; i < iterations; ++i) {
#if defined(__x86_64__)
		asm volatile("add $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0" : "+r"(x));
#elif defined(__aarch64__)
		asm volatile("add %0, %0, #1\n\tadd %0, %0, #1\n\tadd %0, %0, #1\n\tadd %0, %0, #1\n\tadd %0, %0, #1\n\tadd %0, %0, #1\n\tadd %0, %0, #1\n\tadd %0, %0, #1" : "+r"(x));
#else
		return 0;
#endif
	}
	return static_cast<double>(8 * iterations) / static_cast<double>(now_ns() - t0);
}

// ---- (b) stash ----------------------------------------------------------------------------
// Xapiand's configuration (scheduler.h:226-230).
template <class Leaf>
using levels = StashSlots<StashSlots<StashSlots<StashSlots<Leaf, 10, 1 * ms, 50>, 10, 50 * ms, 10>, 12, 500 * ms, 36>,
	4800, 18000 * ms, 4800>;

struct bentry {
	std::atomic<bool> armed{false};
	u64 deadline = 0;
	explicit operator bool() const noexcept { return armed.load(std::memory_order_relaxed); }
};
template <class Leaf, class Entry>
struct stash_wheel {
	std::unique_ptr<levels<Leaf>> q = std::make_unique<levels<Leaf>>();
	StashContext ctx, cctx;
	explicit stash_wheel(u64 base) : ctx(base), cctx(base) {}
	void add(u64 key, Entry* e) { CHECK(q->add(ctx, key, e) == stash_result::placed); }
	// One pass of scheduler's loop (scheduler.h:609-611): checkpoint, walk what is due, clean.
	// The walk stops one tick short of now, so a slot is walked only once every key in it is due.
	template <class Take>
	std::size_t walk(u64 now, Take&& take) {
		auto first = ctx.atom_first_valid_key.load();
		if (first < cctx.atom_first_valid_key.load()) { cctx.atom_first_valid_key = first; }
		cctx.atom_last_valid_key = ctx.atom_last_valid_key.load();
		ctx.op = StashContext::Operation::walk;
		ctx.pending_floor = StashContext::IDLE;
		ctx.begin_key = ctx.atom_first_valid_key.load();
		ctx.end_key = now + 1 - ms;
		Entry* e = nullptr;
		std::size_t n = 0;
		while (q->next(ctx, &e)) { n += take(e); }
		cctx.op = StashContext::Operation::clean;
		cctx.begin_key = cctx.atom_first_valid_key.load();
		cctx.end_key = ctx.atom_first_valid_key.load();
		Entry* ignored = nullptr;
		q->next(cctx, &ignored);
		return n;
	}
};

struct cand_b {
	using entry = bentry;
	static constexpr bool cross = true;
	static constexpr const char* shape = "stash.h as is, entry* values, cancel clears a flag";
	stash_wheel<StashValues<bentry*, 10>, bentry> s;
	explicit cand_b(u64 base) : s(base) {}
	static u64 deadline(const entry& e) { return e.deadline; }
	// A re-arm files the entry again; the walk skips the stale filing (unarmed, or due later).
	void arm(entry& e, u64 d) {
		e.deadline = d;
		e.armed.store(true, std::memory_order_relaxed);
		s.add(d, &e);
	}
	void arm_from(entry& e, u64 d) { arm(e, d); }
	void cancel(entry& e) { e.armed.store(false, std::memory_order_relaxed); }
	template <class F>
	std::size_t expire(u64 now, F&& f) {
		return s.walk(now, [&](bentry* e) -> std::size_t {
			if (!e->armed.load(std::memory_order_relaxed) || e->deadline > now) { return 0; }
			e->armed.store(false, std::memory_order_relaxed);
			f(*e);
			return 1;
		});
	}
};

// ---- (w) the library ----------------------------------------------------------------------
struct cand_w {
	using entry = wheel::timer_entry;
	static constexpr bool cross = true;
	static constexpr const char* shape = "wheel::timer_wheel: 4,096 x 1 ms + six upper levels, 64-stripe inbox";
	std::unique_ptr<wheel::timer_wheel> w;
	explicit cand_w(u64 base) : w(std::make_unique<wheel::timer_wheel>(base)) {}
	static u64 deadline(const entry& e) { return e.deadline; }
	void arm(entry& e, u64 d) { w->arm(e, d); }
	void arm_from(entry& e, u64 d) { w->arm_remote(e, d); }
	void cancel(entry& e) { w->cancel(e); }
	template <class F>
	std::size_t expire(u64 now, F&& f) { return w->expire(now, [&](entry& e) { f(e); }); }
};

template <class F>
auto dispatch(std::string_view arm, F&& f) {
	if (arm == "b" || arm == "b2") { return f(static_cast<cand_b*>(nullptr)); }
	if (arm == "w") { return f(static_cast<cand_w*>(nullptr)); }
	std::fprintf(stderr, "unknown arm %.*s (b, b2 or w)\n", int(arm.size()), arm.data());
	std::exit(2);
}

// ---- single loop --------------------------------------------------------------------------
// Each repetition: arm every entry, re-arm every entry, cancel every entry (each pass in one
// random order), arm every entry again, then expire in 1 ms steps until all fired. The manual
// clock moves 2,002 ms per repetition; at least 1,000,000 / n repetitions, and more until the
// timed passes add up to 100 ms of wall time (A5: one sample's A/A band at n = 1,000).
template <class C>
void single(std::string_view arm, std::size_t n) {
	const std::size_t min_reps = std::max<std::size_t>(1, 1'000'000 / n);
	constexpr u64 min_timed_ns = 100 * ms;
	std::mt19937_64 random(n);
	std::vector<std::uint32_t> order(n);
	std::iota(order.begin(), order.end(), 0);
	std::shuffle(order.begin(), order.end(), random);
	auto spread = [&] { return 1 * ms + random() % (1'999 * ms); };
	std::vector<u64> d1(n), d2(n), d3(n);
	for (std::size_t i = 0; i < n; ++i) { d1[i] = spread(); d2[i] = spread(); d3[i] = spread(); }
	std::printf("config row=single arm=%.*s n=%zu min_reps=%zu min_timed_ms=%llu shape=\"%s\" entry_bytes=%zu clock=manual base_ns=%llu\n",
		int(arm.size()), arm.data(), n, min_reps, static_cast<unsigned long long>(min_timed_ns / ms), C::shape, sizeof(typename C::entry), static_cast<unsigned long long>(manual_base));
	std::printf("start=%s\n", utc().c_str());
	const double ghz0 = core_ghz();
	C c(manual_base);
	std::vector<typename C::entry> entries(n);
	u64 t_arm = 0, t_rearm = 0, t_cancel = 0, t_expire = 0;
	std::size_t fired = 0;
	const u64 started = now_ns();
	std::size_t reps = 0;
	for (; reps < min_reps || t_arm + t_rearm + t_cancel + t_expire < min_timed_ns; ++reps) {
		const u64 base = manual_base + reps * 2'002 * ms;
		u64 t0 = now_ns();
		for (auto i : order) { c.arm(entries[i], base + d1[i]); }
		u64 t1 = now_ns();
		for (auto i : order) { c.arm(entries[i], base + d2[i]); }
		u64 t2 = now_ns();
		for (auto i : order) { c.cancel(entries[i]); }
		u64 t3 = now_ns();
		for (auto i : order) { c.arm(entries[i], base + d3[i]); }
		u64 t4 = now_ns();
		std::size_t round = 0;
		for (u64 t = base; t <= base + 2'001 * ms; t += ms) { round += c.expire(t, [](auto&) {}); }
		u64 t5 = now_ns();
		CHECK(round == n);
		fired += round;
		t_arm += t1 - t0; t_rearm += t2 - t1; t_cancel += t3 - t2; t_expire += t5 - t4;
	}
	const double ops = static_cast<double>(n * reps);
	const double arm_ns = t_arm / ops, rearm_ns = t_rearm / ops, cancel_ns = t_cancel / ops, expire_ns = t_expire / ops;
	std::printf("timed_s=%.3f\n", (now_ns() - started) / 1e9);
	const double ghz1 = core_ghz();
	std::printf("row=single arm=%.*s n=%zu reps=%zu arm_ns=%.2f rearm_ns=%.2f cancel_ns=%.2f expire_ns=%.2f score=%.2f fired=%zu ghz0=%.2f ghz1=%.2f\n",
		int(arm.size()), arm.data(), n, reps, arm_ns, rearm_ns, cancel_ns, expire_ns, arm_ns + rearm_ns + cancel_ns + expire_ns, fired, ghz0, ghz1);
	std::printf("end=%s\n", utc().c_str());
}

// ---- cross-thread -------------------------------------------------------------------------
// P producers each arm TOTAL / P distinct entries once, at steady_clock now (read every 64 arms)
// plus uniform [1, window] ms; the owner spins on steady_clock, firing until every entry fired.
struct cross_result {
	double producer_ns = 0, span_ms = 0, last_after_deadline_ms = 0, max_late_ms = 0;
	std::size_t fired = 0, early = 0, twice = 0, missing = 0;
};
template <class C>
cross_result cross_run(std::size_t producers, std::size_t total, u64 window_ms) {
	const std::size_t per = total / producers;
	total = per * producers;
	std::vector<typename C::entry> entries(total);
	std::vector<std::uint32_t> fires(total, 0);
	std::vector<u64> offsets(total);
	std::mt19937_64 random(total + producers);
	for (auto& o : offsets) { o = 1 * ms + random() % ((window_ms - 1) * ms); }
	C c(now_ns());
	std::atomic<std::size_t> ready{0};
	std::atomic<bool> go{false};
	std::vector<u64> elapsed(producers);
	std::vector<std::thread> threads;
	for (std::size_t p = 0; p < producers; ++p) {
		threads.emplace_back([&, p] {
			ready.fetch_add(1);
			while (!go.load(std::memory_order_acquire)) {}
			const u64 t0 = now_ns();
			u64 now = t0;
			for (std::size_t k = 0; k < per; ++k) {
				if ((k & 63) == 0) { now = now_ns(); }
				const std::size_t i = p * per + k;
				c.arm_from(entries[i], now + offsets[i]);
			}
			elapsed[p] = now_ns() - t0;
		});
	}
	while (ready.load() != producers) {}
	cross_result out;
	u64 last_deadline = 0, last_fire = 0, max_late = 0;
	const u64 start = now_ns();
	go.store(true, std::memory_order_release);
	while (out.fired < total) {
		const u64 now = now_ns();
		out.fired += c.expire(now, [&](typename C::entry& e) {
			const u64 d = C::deadline(e);
			if (d > now) { ++out.early; }
			max_late = std::max(max_late, now - std::min(now, d));
			last_deadline = std::max(last_deadline, d);
			++fires[static_cast<std::size_t>(&e - entries.data())];
		});
		last_fire = now;
		if (now - start > 60'000 * ms) { break; }  // a lost entry: report it rather than hang
	}
	for (auto& t : threads) { t.join(); }
	for (auto f : fires) { out.twice += f > 1; out.missing += f == 0; }
	std::sort(elapsed.begin(), elapsed.end());
	out.producer_ns = static_cast<double>(elapsed[producers / 2]) / static_cast<double>(per);
	if (producers % 2 == 0) {
		out.producer_ns = (static_cast<double>(elapsed[producers / 2 - 1]) + static_cast<double>(elapsed[producers / 2])) / 2.0 / static_cast<double>(per);
	}
	out.span_ms = (last_fire - start) / 1e6;
	out.last_after_deadline_ms = (last_fire > last_deadline ? last_fire - last_deadline : 0) / 1e6;
	out.max_late_ms = max_late / 1e6;
	return out;
}
template <class C>
void cross(std::string_view arm, std::size_t producers, std::size_t total) {
	if constexpr (!C::cross) {
		std::fprintf(stderr, "arm %.*s has no cross-thread arm\n", int(arm.size()), arm.data());
		std::exit(2);
	} else {
		std::printf("config row=cross arm=%.*s producers=%zu total=%zu window_ms=1000 shape=\"%s\" entry_bytes=%zu clock=steady hw_threads=%u\n",
			int(arm.size()), arm.data(), producers, total, C::shape, sizeof(typename C::entry), std::thread::hardware_concurrency());
		std::printf("start=%s\n", utc().c_str());
		const double ghz0 = core_ghz();
		const u64 started = now_ns();
		const auto r = cross_run<C>(producers, total, 1'000);
		std::printf("timed_s=%.3f\n", (now_ns() - started) / 1e9);
		const double ghz1 = core_ghz();
		const bool valid = r.early == 0 && r.twice == 0 && r.missing == 0 && r.last_after_deadline_ms <= 100.0;
		std::printf("row=cross arm=%.*s p=%zu producer_ns=%.2f span_ms=%.1f last_after_deadline_ms=%.2f max_late_ms=%.2f fired=%zu early=%zu twice=%zu missing=%zu valid=%d ghz0=%.2f ghz1=%.2f\n",
			int(arm.size()), arm.data(), producers, r.producer_ns, r.span_ms, r.last_after_deadline_ms, r.max_late_ms, r.fired, r.early, r.twice, r.missing, int(valid), ghz0, ghz1);
		std::printf("end=%s\n", utc().c_str());
		CHECK(r.early == 0 && r.twice == 0 && r.missing == 0);
	}
}

// ---- memory -------------------------------------------------------------------------------
template <class C>
void mem(std::string_view arm, std::size_t n) {
#ifndef COUNT_HEAP
	(void)arm; (void)n;
	std::fprintf(stderr, "mem needs the COUNT_HEAP build\n");
	std::exit(2);
#else
	std::mt19937_64 random(n);
	auto spread = [&] { return 1 * ms + random() % (1'999 * ms); };
	std::vector<typename C::entry> entries(n);
	const auto h0 = probe::heap();
	auto c = std::make_unique<C>(manual_base);
	const auto h1 = probe::heap();
	for (auto& e : entries) { c->arm(e, manual_base + spread()); }
	const auto h2 = probe::heap();
	for (auto& e : entries) { c->arm(e, manual_base + spread()); }
	const auto h3 = probe::heap();
	std::size_t fired = 0;
	for (u64 t = manual_base; t <= manual_base + 2'001 * ms; t += ms) { fired += c->expire(t, [](auto&) {}); }
	CHECK(fired == n);
	for (int k = 0; k < 4; ++k) { c->expire(manual_base + (2'002 + 18'000 * k) * ms, [](auto&) {}); }  // let clean pass every level
	const auto h4 = probe::heap();
	const double wheel_heap = static_cast<double>(h1 - h0);
	std::printf("row=mem arm=%.*s n=%zu entry_bytes=%zu wheel_heap=%.0f heap_per_armed_entry=%.2f bytes_per_entry=%.2f heap_per_rearm=%.2f heap_after_expire=%.0f\n",
		int(arm.size()), arm.data(), n, sizeof(typename C::entry), wheel_heap, static_cast<double>(h2 - h1) / n,
		sizeof(typename C::entry) + static_cast<double>(h2 - h0) / n, static_cast<double>(h3 - h2) / n, static_cast<double>(h4 - h0));
#endif
}

// ---- tests --------------------------------------------------------------------------------
// Random arm, re-arm and cancel against a reference on a manual clock that starts far below
// steady_clock: an entry fires only when due and at most one tick plus one step late, exactly
// once per arm; a cancelled or re-armed arm never fires.
template <class C>
void model(const char* name) {
	constexpr std::size_t n = 512;
	std::mt19937_64 random(20261111);
	std::vector<typename C::entry> entries(n);
	std::map<std::size_t, u64> armed;
	C c(manual_base);
	u64 now = manual_base, fires = 0;
	for (int round = 0; round < 20'000; ++round) {
		for (int k = 0; k < 8; ++k) {
			const std::size_t i = random() % n;
			if (random() % 4 == 0) {
				c.cancel(entries[i]);
				armed.erase(i);
			} else {
				const u64 at = now + 1 + random() % (3'000 * ms);
				c.arm(entries[i], at);
				armed[i] = at;
			}
		}
		const u64 step = random() % (3 * ms);
		now += step;
		c.expire(now, [&](typename C::entry& e) {
			const std::size_t i = static_cast<std::size_t>(&e - entries.data());
			CHECK(armed.count(i) == 1);
			CHECK(armed[i] == C::deadline(e));
			CHECK(armed[i] <= now);
			armed.erase(i);
			++fires;
		});
		for (const auto& [i, at] : armed) { CHECK(at + ms + 3 * ms > now); }  // none overdue
	}
	std::printf("test model arm=%s rounds=20000 fires=%llu ok\n", name, static_cast<unsigned long long>(fires));
}
template <class C>
void cross_check(const char* name) {
	for (std::size_t p : {1, 4, 14}) {
		const auto r = cross_run<C>(p, 140'000, 50);
		CHECK(r.early == 0 && r.twice == 0 && r.missing == 0 && r.fired == 140'000 / p * p);
		std::printf("test cross arm=%s p=%zu fired=%zu ok\n", name, p, r.fired);
	}
}
void test() {
	model<cand_b>("b");
	model<cand_w>("w");
	cross_check<cand_b>("b");
	cross_check<cand_w>("w");
	std::printf("test all ok\n");
}
}  // namespace

int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	const std::string_view mode = argc > 1 ? argv[1] : "";
	if (mode == "test") {
		test();
		return 0;
	}
	if (argc < 4) {
		std::fprintf(stderr, "usage: %s single|mem ARM N | cross ARM P TOTAL | test\n", argv[0]);
		return 2;
	}
	const std::string_view arm = argv[2];
	const std::size_t x = std::strtoull(argv[3], nullptr, 10);
	if (mode == "single") { dispatch(arm, [&](auto* tag) { single<std::remove_pointer_t<decltype(tag)>>(arm, x); }); }
	else if (mode == "mem") { dispatch(arm, [&](auto* tag) { mem<std::remove_pointer_t<decltype(tag)>>(arm, x); }); }
	else if (mode == "cross") {
		const std::size_t total = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 1'000'000;
		dispatch(arm, [&](auto* tag) { cross<std::remove_pointer_t<decltype(tag)>>(arm, x, total); });
	} else {
		std::fprintf(stderr, "unknown mode\n");
		return 2;
	}
	return 0;
}
