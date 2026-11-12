// The stash-niche rows of docs/design/stash-niche.md: many producers file keys over a horizon,
// then one consumer drains them in key order, no cancel and no re-arm. The arms: (b) stash as it is
// (wheel_vs_stash.cc's pieces, included unchanged) and (w) Kronuz/wheel's wheel.hh at wheel:50665c8
// (stash_niche_wheel.hh, a copy).
//   niche ARM P N HORIZON_MS   P producers file N entries; one consumer drains them in key order
//   nichemem ARM P N HORIZON_MS  (the COUNT_HEAP build) the structure's heap per key after filing
//   nichetest                  both arms against every check, small
// ARM is b (stash, cand_nb below) or w (cand_w below).
#define main wheel_vs_stash_main
#include "wheel_vs_stash.cc"
#undef main

#include "stash_niche_wheel.hh"

#include <sys/resource.h>

namespace {
// (b) for the niche: cand_b over Xapiand's four levels with the top level one slot wider (4,801 x
// 18 s). The configuration's span is exactly 24 h, but stash measures it from the slot holding the
// first valid key, so a base inside a slot refuses the last seconds of a 24 h horizon (past_horizon).
template <class Leaf>
using niche_levels = StashSlots<StashSlots<StashSlots<StashSlots<Leaf, 10, 1 * ms, 50>, 10, 50 * ms, 10>, 12, 500 * ms, 36>,
	4801, 18000 * ms, 4801>;
struct niche_stash_wheel {  // stash_wheel with niche_levels, otherwise the same
	std::unique_ptr<niche_levels<StashValues<bentry*, 10>>> q = std::make_unique<niche_levels<StashValues<bentry*, 10>>>();
	StashContext ctx, cctx;
	explicit niche_stash_wheel(u64 base) : ctx(base), cctx(base) {}
	void add(u64 key, bentry* e) { CHECK(q->add(ctx, key, e) == stash_result::placed); }
	template <class Take>
	std::size_t walk(u64 now, Take&& take) {
		auto first = ctx.atom_first_valid_key.load();
		if (first < cctx.atom_first_valid_key.load()) { cctx.atom_first_valid_key = first; }
		cctx.atom_last_valid_key = ctx.atom_last_valid_key.load();
		ctx.op = StashContext::Operation::walk;
		ctx.pending_floor = StashContext::IDLE;
		ctx.begin_key = ctx.atom_first_valid_key.load();
		ctx.end_key = now + 1 - ms;
		bentry* e = nullptr;
		std::size_t n = 0;
		while (q->next(ctx, &e)) { n += take(e); }
		cctx.op = StashContext::Operation::clean;
		cctx.begin_key = cctx.atom_first_valid_key.load();
		cctx.end_key = ctx.atom_first_valid_key.load();
		bentry* ignored = nullptr;
		q->next(cctx, &ignored);
		return n;
	}
};
struct cand_nb {  // cand_b on niche_stash_wheel
	using entry = bentry;
	static constexpr bool cross = true;
	static constexpr const char* shape = "stash.h, four levels (top 4,801 x 18 s), entry* values";
	niche_stash_wheel s;
	explicit cand_nb(u64 base) : s(base) {}
	static u64 deadline(const entry& e) { return e.deadline; }
	void arm_from(entry& e, u64 d) {
		e.deadline = d;
		e.armed.store(true, std::memory_order_relaxed);
		s.add(d, &e);
	}
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

// (w): the library wheel; another thread's arm is `arm_remote`, the owner's loop is expire(now).
struct cand_w {
	using entry = wheel::timer_entry;
	static constexpr bool cross = true;
	static constexpr const char* shape = "Kronuz/wheel wheel.hh: 4,096 x 1 ms + six upper levels, 64-stripe inbox";
	std::unique_ptr<wheel::timer_wheel> w;
	explicit cand_w(u64 base) : w(std::make_unique<wheel::timer_wheel>(base)) {}
	static u64 deadline(const entry& e) { return e.deadline; }
	void arm_from(entry& e, u64 d) { w->arm_remote(e, d); }
	template <class F>
	std::size_t expire(u64 now, F&& f) { return w->expire(now, [&](entry& e) { f(e); }); }
};

// Peak resident set so far, in bytes (macOS reports bytes, Linux kilobytes).
std::size_t peak_rss() {
	rusage u{};
	getrusage(RUSAGE_SELF, &u);
#ifdef __APPLE__
	return static_cast<std::size_t>(u.ru_maxrss);
#else
	return static_cast<std::size_t>(u.ru_maxrss) * 1024;
#endif
}

// What a drain reports; `drive` runs one candidate's consumer and calls `deliver` per entry.
struct drain_stats {
	std::size_t calls = 0, empty_calls = 0;
};
template <class C, class F>
drain_stats drive(C& c, u64 end, u64 give_up_ns, F&& deliver) {
	drain_stats s;
	if constexpr (std::is_same_v<C, cand_w>) {
		// File the inbox, then wake at next() (an occupied slot's end, or an upper slot's start) until empty.
		const u64 started = now_ns();
		u64 t = manual_base;
		for (;;) {
			const std::size_t fired = c.expire(t, [&](auto& e) { deliver(e, t); });
			++s.calls;
			s.empty_calls += fired == 0;
			t = c.w->next();
			if (t == wheel::timer_wheel::never) { break; }
			if ((s.calls & 4095) == 0 && now_ns() - started > give_up_ns) { break; }
		}
	} else {
		(void)give_up_ns;
		const u64 t = end;
		c.expire(t, [&](auto& e) { deliver(e, t); });
		s.calls = 1;
	}
	return s;
}

struct niche_result {
	double insert_ns = 0, drain_ns = 0, rss_growth = 0;
	std::size_t delivered = 0, early = 0, twice = 0, disordered = 0;
	std::size_t calls = 0, empty_calls = 0;
	bool capped = false;
};

template <class C>
niche_result niche_run(std::size_t producers, std::size_t total, u64 horizon_ms, bool with_drain, std::size_t* heap_after_fill = nullptr) {
	const std::size_t per = total / producers;
	total = per * producers;
	std::vector<typename C::entry> entries(total);
	std::vector<u64> offsets(total);
	std::mt19937_64 random(total * 31 + horizon_ms);
	for (auto& o : offsets) { o = 1 * ms + random() % ((horizon_ms - 1) * ms); }
	niche_result out;
	const std::size_t rss0 = peak_rss();
#ifdef COUNT_HEAP
	const auto heap0 = probe::heap();
#endif
	auto owner = std::make_unique<C>(manual_base);
	C& c = *owner;
	std::atomic<std::size_t> ready{0};
	std::atomic<bool> go{false};
	std::vector<u64> elapsed(producers);
	std::vector<std::thread> threads;
	for (std::size_t p = 0; p < producers; ++p) {
		threads.emplace_back([&, p] {
			ready.fetch_add(1);
			while (!go.load(std::memory_order_acquire)) {}
			const u64 t0 = now_ns();
			for (std::size_t k = 0; k < per; ++k) {
				const std::size_t i = p * per + k;
				c.arm_from(entries[i], manual_base + offsets[i]);
			}
			elapsed[p] = now_ns() - t0;
		});
	}
	while (ready.load() != producers) {}
	go.store(true, std::memory_order_release);
	for (auto& t : threads) { t.join(); }
	std::sort(elapsed.begin(), elapsed.end());
	out.insert_ns = static_cast<double>(elapsed[producers / 2]) / static_cast<double>(per);
	if (producers % 2 == 0) {
		out.insert_ns = (static_cast<double>(elapsed[producers / 2 - 1]) + static_cast<double>(elapsed[producers / 2])) / 2.0 / static_cast<double>(per);
	}
#ifdef COUNT_HEAP
	if (heap_after_fill) { *heap_after_fill = static_cast<std::size_t>(probe::heap() - heap0); }
#else
	(void)heap_after_fill;
#endif
	if (with_drain) {
		std::vector<bool> seen(total, false);
		u64 last_tick = 0;
		const u64 end = manual_base + horizon_ms * ms + 2 * ms;
		const u64 give_up_ns = 300'000 * ms;
		const u64 t0 = now_ns();
		const auto stats = drive(c, end, give_up_ns, [&](typename C::entry& e, u64 now) {
			const std::size_t i = static_cast<std::size_t>(&e - entries.data());
			const u64 d = C::deadline(e);
			out.early += d > now;
			out.twice += seen[i];
			seen[i] = true;
			out.disordered += d / ms < last_tick;
			last_tick = std::max(last_tick, d / ms);
			++out.delivered;
		});
		out.drain_ns = static_cast<double>(now_ns() - t0) / static_cast<double>(out.delivered ? out.delivered : 1);
		out.calls = stats.calls;
		out.empty_calls = stats.empty_calls;
		out.capped = out.delivered < total;
		out.rss_growth = static_cast<double>(peak_rss() - std::min(peak_rss(), rss0));
	}
	return out;
}

template <class C>
void niche(std::string_view arm, std::size_t producers, std::size_t total, u64 horizon_ms) {
	std::printf("config row=niche arm=%.*s producers=%zu total=%zu horizon_ms=%llu shape=\"%s\" entry_bytes=%zu clock=manual base_ns=%llu hw_threads=%u\n",
		int(arm.size()), arm.data(), producers, total, static_cast<unsigned long long>(horizon_ms), C::shape, sizeof(typename C::entry),
		static_cast<unsigned long long>(manual_base), std::thread::hardware_concurrency());
	std::printf("start=%s\n", utc().c_str());
	const u64 started = now_ns();
	const auto r = niche_run<C>(producers, total, horizon_ms, true);
	std::printf("timed_s=%.3f\n", (now_ns() - started) / 1e9);
	const bool valid = r.early == 0 && r.twice == 0 && r.disordered == 0 && (r.capped || r.delivered == total / producers * producers);
	std::printf("row=niche arm=%.*s p=%zu n=%zu h_ms=%llu insert_ns=%.2f drain_ns=%.2f cost_ns=%.2f delivered=%zu early=%zu twice=%zu disordered=%zu calls=%zu empty_calls=%zu rss_growth_mb=%.1f capped=%d valid=%d\n",
		int(arm.size()), arm.data(), producers, total, static_cast<unsigned long long>(horizon_ms), r.insert_ns, r.drain_ns, r.insert_ns + r.drain_ns,
		r.delivered, r.early, r.twice, r.disordered, r.calls, r.empty_calls, r.rss_growth / 1048576.0, int(r.capped), int(valid));
	std::printf("end=%s\n", utc().c_str());
	CHECK(r.early == 0 && r.twice == 0 && r.disordered == 0);
}

template <class C>
void nichemem(std::string_view arm, std::size_t producers, std::size_t total, u64 horizon_ms) {
#ifndef COUNT_HEAP
	(void)arm; (void)producers; (void)total; (void)horizon_ms;
	std::fprintf(stderr, "nichemem needs the COUNT_HEAP build\n");
	std::exit(2);
#else
	std::size_t heap = 0;
	niche_run<C>(producers, total, horizon_ms, false, &heap);
	std::printf("row=nichemem arm=%.*s p=%zu n=%zu h_ms=%llu entry_bytes=%zu heap_bytes=%zu heap_per_key=%.2f\n",
		int(arm.size()), arm.data(), producers, total, static_cast<unsigned long long>(horizon_ms), sizeof(typename C::entry), heap,
		static_cast<double>(heap) / static_cast<double>(total / producers * producers));
#endif
}

template <class F>
auto niche_dispatch(std::string_view arm, F&& f) {
	if (arm == "b") { return f(static_cast<cand_nb*>(nullptr)); }
	if (arm == "w") { return f(static_cast<cand_w*>(nullptr)); }
	std::fprintf(stderr, "unknown arm %.*s (b or w)\n", int(arm.size()), arm.data());
	std::exit(2);
}

// Every check the rows rely on, small: delivery is exactly-once and in tick order for both arms
// over both horizons and every producer count, and a missing or doubled entry would be caught.
template <class C>
void check_niche(const char* name) {
	for (u64 h : {u64{1'000}, u64{86'400'000}}) {
		for (std::size_t p : {1, 4, 14}) {
			const auto r = niche_run<C>(p, 14'000, h, true);
			CHECK(r.early == 0 && r.twice == 0 && r.disordered == 0 && r.delivered == 14'000 / p * p && !r.capped);
			std::printf("test niche arm=%s p=%zu h_ms=%llu delivered=%zu ok\n", name, p, static_cast<unsigned long long>(h), r.delivered);
		}
	}
}
}  // namespace

int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	const std::string_view mode = argc > 1 ? argv[1] : "";
	if (mode == "nichetest") {
		{  // the last millisecond of a 24 h horizon is filed, not refused
			cand_nb c(manual_base);
			bentry e;
			c.arm_from(e, manual_base + 86'400'000 * ms - ms);
			std::printf("test far-edge ok\n");
		}
		check_niche<cand_nb>("b");
		check_niche<cand_w>("w");
		std::printf("test all ok\n");
		return 0;
	}
	if (argc < 6 || (mode != "niche" && mode != "nichemem")) {
		std::fprintf(stderr, "usage: %s niche|nichemem ARM P N HORIZON_MS | nichetest\n", argv[0]);
		return 2;
	}
	const std::string_view arm = argv[2];
	const std::size_t p = std::strtoull(argv[3], nullptr, 10), n = std::strtoull(argv[4], nullptr, 10);
	const u64 h = std::strtoull(argv[5], nullptr, 10);
	if (mode == "niche") { niche_dispatch(arm, [&](auto* tag) { niche<std::remove_pointer_t<decltype(tag)>>(arm, p, n, h); }); }
	else { niche_dispatch(arm, [&](auto* tag) { nichemem<std::remove_pointer_t<decltype(tag)>>(arm, p, n, h); }); }
	return 0;
}
