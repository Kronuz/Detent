// Tests of D8's waiter protocol (waiter.hh) and cancellation table over a model group:
// callers on loop B propose to a group on loop A, a deadline on loop C's wheel fires, and
// the group's turn write runs on an IO loop.
//   explore [callers]   every interleaving of the four loops, run one at a time (review F3):
//                       exactly one resume per caller, no list or callback reaching a freed
//                       frame, and the table's rows (review F4): cancelled before admission
//                       to Core never staged; staged in a turn always written and committed
//   naive [callers]     the same over D8 as first written (cancel resumes the caller at once);
//                       passes iff some interleaving reaches a freed frame
//   threads [n]         the four loops on four threads, n rounds; run under TSan and ASan
#include "loops.hh"
#include "waiter.hh"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::uint64_t ms = 1'000'000;

#define CHECK(c)                                                                     \
	do {                                                                             \
		if (!(c)) {                                                                  \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
			std::exit(1);                                                            \
		}                                                                            \
	} while (0)

// A caller's frame: the waiter, and what the caller saw when it resumed.
template <class Loop>
struct frame {
	bench::waiter<Loop> w;
	int resumes = 0, outcome = -1, value = -1;
};

// What every run records, under one lock so the threaded test can share it.
struct ledger {
	std::mutex mutex;
	std::set<const void*> live;       // frames not yet freed
	std::vector<void*> quarantine;    // freed frames in the naive run, kept so no access is a real use-after-free
	int violations = 0;               // an access to a freed frame
	std::map<int, int> outcome;       // caller -> outcome
	std::map<int, int> value;         // caller -> placed record
	std::map<int, int> resumes;
	bool alive(const void* w) {
		std::lock_guard lock(mutex);
		if (live.count(w)) { return true; }
		++violations;
		return false;
	}
};

// The group's machine on loop A: an admission list, then Core (stages a record in the turn
// log), then the turn's write on the IO loop, then the barrier: the record is durable,
// Committed reaches the sink, and the proposal's waiter completes if it still waits.
template <class Loop>
struct group_model {
	using waiter = bench::waiter<Loop>;
	Loop& loop;
	Loop& io;
	ledger& book;
	waiter* admission = nullptr;
	waiter* placed = nullptr;
	std::map<const waiter*, int> record_of;
	std::set<int> staged, written, committed;
	int next_record = 1;

	void enqueue(waiter& w, const task::cancel_token& token) {
		w.link(admission);
		w.watch(token);
		loop.post([this] { turn(); });
	}
	void turn() {  // Core accepts the oldest proposal still waiting for admission
		waiter* w = admission;
		if (!w) { return; }  // its caller was cancelled before admission: never submitted
		if (!book.alive(w)) { return; }
		while (w->next) {
			if (!book.alive(w->next)) { return; }
			w = w->next;
		}
		w->unlink();
		const int r = next_record++;
		staged.insert(r);  // machine-owned IO from here: written under the loop's token only
		record_of[w] = r;
		w->link(placed);
		io.post([this, r] { loop.post([this, r] { barrier(r); }); });
	}
	void barrier(int r) {
		written.insert(r);
		committed.insert(r);
		for (waiter* w = placed; w;) {
			if (!book.alive(w)) { return; }
			waiter* following = w->next;
			if (record_of[w] == r) {
				w->complete(r);
				return;
			}
			w = following;
		}
	}
};

// One run of `callers` proposals sharing one fan-out deadline, on loops of type Loop.
template <class Loop>
struct scenario {
	Loop &a, &b, &c, &io;
	ledger book;
	group_model<Loop> group{a, io, book};
	std::unique_ptr<bench::wheel_deadline> deadline;
	task::cancel_token token;
	bool naive;
	std::atomic<int> resumed{0};

	scenario(Loop& a_, Loop& b_, Loop& c_, Loop& io_, bool naive_) : a(a_), b(b_), c(c_), io(io_), naive(naive_) {}

	void propose(int id) {  // on B
		auto* f = new frame<Loop>;
		{
			std::lock_guard lock(book.mutex);
			book.live.insert(&f->w);
		}
		f->w.caller = &b;
		f->w.group = &a;
		f->w.naive = naive;
		f->w.resume = [this, f, id] {  // on B: the caller resumes and its frame dies
			{
				std::lock_guard lock(book.mutex);
				book.resumes[id] += 1;
				book.outcome[id] = f->w.state.load();
				book.value[id] = f->w.state.load() == f->w.completed ? f->w.value : -1;
				book.live.erase(&f->w);
			}
			f->w.callback.reset();  // deregisters; waits for a callback running on another thread
			if (naive) {
				std::lock_guard lock(book.mutex);
				book.quarantine.push_back(f);
			} else {
				delete f;
			}
			++resumed;
		};
		task::cancel_token t = token;
		a.post([this, f, t] { group.enqueue(f->w, t); });  // the call hops to the group's loop
	}
	~scenario() {
		for (void* f : book.quarantine) { delete static_cast<frame<Loop>*>(f); }
	}
};

// Checks one finished run against the protocol and the cancellation table.
template <class Loop>
void verdict(scenario<Loop>& s, int callers) {
	CHECK(s.book.violations == 0);
	CHECK(!s.group.admission && !s.group.placed);
	for (int id = 0; id < callers; ++id) { CHECK(s.book.resumes[id] == 1); }
	std::set<int> values;
	for (int id = 0; id < callers; ++id) {
		if (s.book.outcome[id] == bench::waiter<Loop>::completed) {
			CHECK(s.group.committed.count(s.book.value[id]));
			CHECK(values.insert(s.book.value[id]).second);
		} else {
			CHECK(s.book.outcome[id] == bench::waiter<Loop>::cancelled && s.deadline->expired());
		}
	}
	// A staged record is written and committed whatever happened to its caller; a proposal
	// cancelled before admission was never staged, so staged records number the admitted ones.
	CHECK(s.group.staged == s.group.written && s.group.written == s.group.committed);
	CHECK(s.group.staged.size() <= std::size_t(callers));
}

// Runs a schedule: `prefix` picks the loop for each step, then returns the loops that can step.
struct replay {
	int callers;
	bool naive;
	std::vector<int> enabled;
	int violations = 0;
	bool run(const std::vector<int>& prefix) {
		bench::manual_loop a, b, c, io;
		scenario<bench::manual_loop> s(a, b, c, io, naive);
		s.deadline = std::make_unique<bench::wheel_deadline>(c.timers, 5 * ms);
		s.token = s.deadline->token();
		for (int id = 0; id < callers; ++id) { b.post([&s, id] { s.propose(id); }); }
		c.post([&] { c.timers.expire(10 * ms); });  // the deadline fires
		bench::manual_loop* loops[] = {&a, &b, &c, &io};
		for (int pick : prefix) { loops[pick]->step(); }
		enabled.clear();
		for (int i = 0; i < 4; ++i) { if (!loops[i]->idle()) { enabled.push_back(i); } }
		if (!enabled.empty()) { return false; }
		if (naive) {
			violations += s.book.violations > 0 || [&] {
				for (int id = 0; id < callers; ++id) { if (s.book.resumes[id] != 1) { return true; } }
				return false;
			}();
		} else {
			verdict(s, callers);
		}
		return true;
	}
};

void explore(int callers, bool naive) {
	replay r{callers, naive, {}};
	std::vector<int> prefix;
	std::uint64_t schedules = 0, steps = 0;
	// Depth-first over every choice of the next loop to step, replaying each prefix from scratch.
	std::vector<std::vector<int>> pending{{}};
	while (!pending.empty()) {
		prefix = std::move(pending.back());
		pending.pop_back();
		steps += prefix.size();
		if (r.run(prefix)) {
			++schedules;
			continue;
		}
		for (auto it = r.enabled.rbegin(); it != r.enabled.rend(); ++it) {
			auto next = prefix;
			next.push_back(*it);
			pending.push_back(std::move(next));
		}
	}
	if (naive) {
		CHECK(r.violations > 0);
		std::printf("naive callers=%d schedules=%llu schedules_reaching_a_freed_frame=%d (expected > 0) ok\n", callers,
			static_cast<unsigned long long>(schedules), r.violations);
	} else {
		std::printf("explore callers=%d schedules=%llu steps=%llu violations=0 ok\n", callers, static_cast<unsigned long long>(schedules),
			static_cast<unsigned long long>(steps));
	}
}

// The same protocol on four threads: the deadline fires on C while B's callers and A's group
// race it; every round drains all four loops and checks the verdict.
void threads(int rounds) {
	bench::thread_loop a, b, c, io;
	std::mt19937_64 random(11);
	int cancelled = 0, completed = 0;
	constexpr int callers = 3;
	for (int round = 0; round < rounds; ++round) {
		scenario<bench::thread_loop> s(a, b, c, io, false);
		std::atomic<bool> ready{false};
		c.post([&] {
			s.deadline = std::make_unique<bench::wheel_deadline>(c.timers, (round + 1) * ms);
			s.token = s.deadline->token();
			ready = true;
		});
		while (!ready) { std::this_thread::yield(); }
		for (int id = 0; id < callers; ++id) { b.post([&s, id] { s.propose(id); }); }
		const unsigned spin = unsigned(random() % 400000);
		c.post([&, spin] {
			for (unsigned k = 0; k < spin; ++k) { std::atomic_signal_fence(std::memory_order_seq_cst); }
			c.timers.expire((round + 1) * ms);
		});
		while (s.resumed.load() < callers) { std::this_thread::yield(); }
		// Drain: each loop runs a marker after everything queued before it, twice for the hops.
		for (int pass = 0; pass < 3; ++pass) {
			std::atomic<int> marks{0};
			for (auto* l : {&a, &io, &c, &b}) { l->post([&] { ++marks; }); }
			while (marks.load() < 4) { std::this_thread::yield(); }
		}
		std::atomic<bool> checked{false};
		a.post([&] {
			verdict(s, callers);
			checked = true;
		});
		while (!checked) { std::this_thread::yield(); }
		for (int id = 0; id < callers; ++id) { (s.book.outcome[id] == 1 ? completed : cancelled) += 1; }
		std::atomic<bool> gone{false};
		c.post([&] {
			s.deadline.reset();
			gone = true;
		});
		while (!gone) { std::this_thread::yield(); }
	}
	CHECK(completed > 0 && cancelled > 0);  // both sides of the race were reached
	std::printf("threads rounds=%d callers=%d completed=%d cancelled=%d ok\n", rounds, callers, completed, cancelled);
}
}  // namespace

int main(int argc, char** argv) {
	const std::string_view only = argc > 1 ? argv[1] : "all";
	const int n = argc > 2 ? std::atoi(argv[2]) : 0;
	if (only == "all" || only == "explore") {
		for (int callers : n ? std::vector<int>{n} : std::vector<int>{1, 2}) { explore(callers, false); }
	}
	if (only == "all" || only == "naive") {
		for (int callers : n ? std::vector<int>{n} : std::vector<int>{1, 2}) { explore(callers, true); }
	}
	if (only == "all" || only == "threads") { threads(n ? n : 3000); }
	std::printf("waiter_test: ok\n");
	return 0;
}
