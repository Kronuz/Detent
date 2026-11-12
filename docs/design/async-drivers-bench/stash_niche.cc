// The stash-niche rows of docs/design/stash-niche.md: many producers file keys over a horizon,
// then one consumer drains them in key order, no cancel and no re-arm. The arms are wheel-evidence's
// own: (b) stash as it is, (f) the striped-inbox wheel (wheel_vs_stash.cc, included unchanged).
//   niche ARM P N HORIZON_MS   P producers file N entries; one consumer drains them in key order
//   nichemem ARM P N HORIZON_MS  (the COUNT_HEAP build) the structure's heap per key after filing
//   nichetest                  both arms against every check, small
// ARM is b or f.
#define main wheel_vs_stash_main
#include "wheel_vs_stash.cc"
#undef main

#include <sys/resource.h>

namespace {
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
	if constexpr (std::is_same_v<C, cand_f>) {
		// File the inbox, then wake at the end of each next occupied slot until the wheel is empty.
		const u64 started = now_ns();
		u64 t = manual_base;
		for (;;) {
			const std::size_t fired = c.expire(t, [&](auto& e) { deliver(e, t); });
			++s.calls;
			s.empty_calls += fired == 0;
			t = c.w->next();
			if (t == ~u64{0}) { break; }
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
	if (arm == "b") { return f(static_cast<cand_b*>(nullptr)); }
	if (arm == "f") { return f(static_cast<cand_f*>(nullptr)); }
	std::fprintf(stderr, "unknown arm %.*s (b or f)\n", int(arm.size()), arm.data());
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
		check_niche<cand_b>("b");
		check_niche<cand_f>("f");
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
