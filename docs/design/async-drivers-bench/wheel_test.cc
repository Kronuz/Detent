// Tests of the wheel serving task deadlines (docs/design/async-drivers.md, D4 and D8):
//   reentry    a fire cancels or re-arms another due entry, or itself (review F1.3, F2)
//   model      random arm, cancel and re-arm from fires against a reference model
//   long       deadlines beyond one 4,096 ms round: never early, the early wakes counted
//   manual     a deadline on the manual clock of task::inline_scheduler, no real time
//   chain      a fired parent deadline cancels a linked child whose callback destroys it
//   loops      a parent on loop A, children on loop B: each deadline lives on its own wheel
// Prints one line per test; exits nonzero on the first failure. Built with assertions on.
#include "loops.hh"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string_view>
#include <vector>

using bench::wheel;
using bench::wheel_entry;

namespace {
constexpr std::uint64_t ms = 1'000'000;

#define CHECK(c)                                                                     \
	do {                                                                             \
		if (!(c)) {                                                                  \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
			std::exit(1);                                                            \
		}                                                                            \
	} while (0)

// The reviewer's probe (261109-233203-d822ec) and its variants, on one slot.
void reentry() {
	for (const std::string_view mode : {"cancel", "rearm", "self", "arm-due"}) {
		wheel w(0);
		wheel_entry a, b, c;
		w.arm(a, 500'000);  // both due in slot 0; b is the list head, a follows
		w.arm(b, 600'000);
		int fired_a = 0, fired_b = 0, fired_c = 0;
		auto fire = [&](wheel_entry& e) {
			if (&e == &b) {
				++fired_b;
				if (mode == "cancel") { w.cancel(a); }
				if (mode == "rearm") { w.arm(a, 50 * ms); }
				if (mode == "self" && fired_b == 1) { w.arm(b, 1 * ms); }  // due now: fires on the next expire
				if (mode == "arm-due") { w.arm(c, 0); }         // a new entry already due
			} else if (&e == &a) {
				++fired_a;
			} else {
				++fired_c;
			}
		};
		const auto first = w.expire(1 * ms, fire);
		if (mode == "cancel") { CHECK(first == 1 && fired_a == 0 && !a.armed() && w.size() == 0); }
		if (mode == "rearm") {
			CHECK(first == 1 && fired_a == 0 && a.armed() && w.size() == 1);
			CHECK(w.expire(49 * ms, fire) == 0);
			CHECK(w.expire(50 * ms, fire) == 1 && fired_a == 1);
		}
		if (mode == "self") {
			CHECK(first == 2 && fired_b == 1 && b.armed());
			CHECK(w.expire(1 * ms, fire) == 1 && fired_b == 2 && w.size() == 0);
		}
		if (mode == "arm-due") {
			CHECK(first == 2 && fired_c == 0 && c.armed());
			CHECK(w.expire(1 * ms, fire) == 1 && fired_c == 1);
		}
		std::printf("reentry mode=%.*s ok\n", int(mode.size()), mode.data());
	}
}

// Random operations against a reference: an entry fires only when due, exactly once per arm,
// and every entry due when expire() began fires in it unless a fire cancelled or re-armed it.
void model() {
	constexpr std::size_t n = 512;
	std::mt19937_64 random(20261109);
	std::vector<wheel_entry> entries(n);
	std::map<const wheel_entry*, std::uint64_t> armed;  // the reference: entry -> deadline
	wheel w(0);
	std::uint64_t now = 0, fires = 0, mutations = 0;
	for (int round = 0; round < 20000; ++round) {
		for (int k = 0; k < 8; ++k) {
			auto& e = entries[random() % n];
			if (random() % 4 == 0) {
				w.cancel(e);
				armed.erase(&e);
			} else {
				const std::uint64_t at = now + random() % (12'000 * ms);  // up to three rounds ahead
				w.arm(e, at);
				armed[&e] = at;
			}
		}
		now += random() % (3 * ms);
		if (random() % 500 == 0) { now += 9'000 * ms; }  // a gap longer than a round
		std::set<const wheel_entry*> due;
		for (const auto& [e, at] : armed) { if (at <= now) { due.insert(e); } }
		w.expire(now, [&](wheel_entry& e) {
			CHECK(due.count(&e) == 1);  // due when expire began, never fired twice
			CHECK(armed.at(&e) <= now);
			due.erase(&e);
			armed.erase(&e);
			++fires;
			if (random() % 3 == 0) {  // a fire mutates another entry
				auto& other = entries[random() % n];
				due.erase(&other);
				++mutations;
				if (random() % 2) {
					w.cancel(other);
					armed.erase(&other);
				} else {
					const std::uint64_t at = now + random() % (5'000 * ms);
					w.arm(other, at);
					armed[&other] = at;
				}
			}
		});
		CHECK(due.empty());
		CHECK(w.size() == armed.size());
		for (const auto& [e, at] : armed) { CHECK(e->armed() && e->deadline == at); }
	}
	std::printf("model rounds=20000 fires=%llu mutations_from_fires=%llu ok\n", static_cast<unsigned long long>(fires),
		static_cast<unsigned long long>(mutations));
}

// The reactor arms its timer at next() and expires when it fires. A deadline beyond one round
// fires in the slot of its own tick, never before it, after one early wake per round passed.
void long_deadlines() {
	for (const std::uint64_t d : {4'095ull, 4'096ull, 10'000ull, 30'000ull, 300'000ull}) {
		wheel w(0);
		wheel_entry e;
		bool fired = false;
		std::uint64_t fired_at = 0, wakes = 0;
		w.arm(e, d * ms + 123);
		for (std::uint64_t now = 0; !fired;) {
			now = w.next();
			CHECK(now != ~std::uint64_t{0});
			++wakes;
			w.expire(now, [&](wheel_entry&) { fired = true; fired_at = now; });
		}
		const std::uint64_t rounds = d / wheel::slots;  // whole rounds before the deadline's own
		CHECK(fired_at >= d * ms + 123 && fired_at - (d * ms + 123) <= wheel::tick);
		CHECK(wakes == rounds + 1);
		std::printf("long deadline_ms=%llu wakes=%llu early_wakes=%llu late_ns=%llu ok\n", static_cast<unsigned long long>(d),
			static_cast<unsigned long long>(wakes), static_cast<unsigned long long>(wakes - 1),
			static_cast<unsigned long long>(fired_at - (d * ms + 123)));
	}
}

// A request on the simulator's clock: inline_scheduler::now() drives the wheel, a deadline's
// cancel callback posts the waiting frame, and no real time passes.
task::task<int> wait_for_cancel(task::cancel_token token) {
	struct awaiter {
		task::cancel_token token;
		std::coroutine_handle<> handle;
		task::scheduler_ref where;
		struct resume {
			awaiter* self;
			void operator()() noexcept { self->where.post(self->handle); }
		};
		std::optional<task::cancel_callback<resume>> callback;
		bool await_ready() const noexcept { return token.requested(); }
		void await_suspend(std::coroutine_handle<> h) {
			handle = h;
			where = task::current();
			callback.emplace(token, resume{this});
		}
		int await_resume() const noexcept { return int(token.reason()); }
	};
	co_return co_await awaiter{token, {}, {}, {}};
}

void manual_clock() {
	task::inline_scheduler s;
	wheel w(s.now());
	const auto start = std::chrono::steady_clock::now();
	bench::wheel_deadline d(w, s.now() + 5'000 * ms);  // five seconds of simulated time
	int reason = -1;
	task::spawn(task::scheduler_ref::of(s), wait_for_cancel(d.token()), [&](std::exception_ptr, std::optional<int> r) { reason = *r; });
	s.run();
	CHECK(reason == -1 && !d.expired());
	s.advance(4'999 * ms);
	w.expire(s.now());
	s.run();
	CHECK(reason == -1 && !d.expired());
	s.advance(1 * ms);
	CHECK(w.expire(s.now()) == 1);
	s.run();
	CHECK(d.expired() && reason == int(task::cancel_reason::deadline));
	const auto real = std::chrono::steady_clock::now() - start;
	CHECK(real < std::chrono::seconds(1));
	std::printf("manual simulated_ms=%llu real_us=%lld ok\n", static_cast<unsigned long long>(s.now() / ms),
		static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(real).count()));
}

// A fan-out root's deadline and a child's tighter one, both due in one expire. The parent's
// fire cancels the child's token, whose callback destroys the child deadline (its entry is on
// the due list, not yet fired): the shape that crashed the original prototype.
void chain() {
	for (int order = 0; order < 2; ++order) {
		wheel w(0);
		auto parent = std::make_unique<bench::wheel_deadline>(w, order ? 3 * ms : 2 * ms);
		auto child = std::make_unique<bench::wheel_deadline>(w, order ? 2 * ms : 3 * ms, parent->token());
		const auto child_token = child->token();
		bool destroyed = false;
		task::cancel_callback on_cancel(parent->token(), [&] {
			child.reset();  // the request on the child's state is running or done; its entry unlinks
			destroyed = true;
		});
		const auto fired = w.expire(4 * ms);
		CHECK(destroyed && child_token.requested() && w.size() == 0);
		CHECK(fired == (order ? 2u : 1u));  // the child fired first when it was due first
		std::printf("chain order=%s fired=%zu ok\n", order ? "child-first" : "parent-first", fired);
	}
}

// A parent deadline on loop A, its children on loop B, created and destroyed on B while A fires
// the parent. Each deadline's entry is touched only on its own loop (the wheels assert it);
// the link between them is task's thread-safe linked token. Run under TSan and ASan.
void loops() {
	constexpr int iterations = 2000, children = 8;
	std::atomic<int> callbacks{0};
	auto a = std::make_unique<bench::thread_loop>();
	auto b = std::make_unique<bench::thread_loop>();
	std::mt19937_64 random(7);
	for (int i = 0; i < iterations; ++i) {
		std::atomic<int> done{0};
		std::unique_ptr<bench::wheel_deadline> parent;
		task::cancel_token parent_token;
		std::atomic<bool> armed{false};
		a->post([&] {
			parent = std::make_unique<bench::wheel_deadline>(a->timers, (i + 1) * ms);
			parent_token = parent->token();
			armed = true;
		});
		while (!armed) { std::this_thread::yield(); }
		const unsigned spin = unsigned(random() % 2000), keep = unsigned(random() % (children + 1));
		b->post([&, spin, keep] {
			struct child {
				bench::wheel_deadline deadline;
				task::cancel_callback<std::function<void()>> callback;
				child(bench::wheel& w, std::uint64_t at, const task::cancel_token& parent, std::atomic<int>& count)
					: deadline(w, at, parent), callback(deadline.token(), std::function<void()>([&count] { ++count; })) {}
			};
			std::vector<std::unique_ptr<child>> mine;
			for (int c = 0; c < children; ++c) { mine.push_back(std::make_unique<child>(b->timers, (i + 1000) * ms, parent_token, callbacks)); }
			++done;  // A may fire now
			for (unsigned k = 0; k < spin; ++k) { std::atomic_signal_fence(std::memory_order_seq_cst); }
			mine.resize(keep);  // some die while A fires, the rest after
			while (done.load() < 2) { std::this_thread::yield(); }
			mine.clear();
			++done;
		});
		while (done.load() < 1) { std::this_thread::yield(); }
		a->post([&] {
			CHECK(a->timers.expire((i + 1) * ms) == 1 && parent->expired());
			parent.reset();
			++done;
		});
		while (done.load() < 3) { std::this_thread::yield(); }
	}
	a.reset();
	b.reset();
	std::printf("loops iterations=%d children=%d child_callbacks=%d ok\n", iterations, children, callbacks.load());
}
}  // namespace

int main(int argc, char** argv) {
	const std::string_view only = argc > 1 ? argv[1] : "all";
	if (only == "all" || only == "reentry") { reentry(); }
	if (only == "all" || only == "model") { model(); }
	if (only == "all" || only == "long") { long_deadlines(); }
	if (only == "all" || only == "manual") { manual_clock(); }
	if (only == "all" || only == "chain") { chain(); }
	if (only == "all" || only == "loops") { loops(); }
	std::printf("wheel_test: ok\n");
	return 0;
}
