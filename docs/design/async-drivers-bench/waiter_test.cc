// Tests of D8's waiter protocol (waiter.hh) and cancellation table over a model group:
// callers on loop B propose to a group on loop A, a deadline on loop C's wheel fires, and
// the group's turn write runs on an IO loop.
//   explore [callers]   every interleaving of the four loops, run one at a time (review F3),
//                       where each post to another loop is also a scheduling point: the target
//                       may run the posted call before the poster's next statement (review F14).
//                       Checks exactly one resume per caller, no list, callback or waiter method
//                       reaching a freed frame (freed frames are kept and must stay as they were
//                       when freed), and the table's rows (review F4): cancelled before admission
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

// A caller's frame: the waiter, and its waiter's fields when the caller resumed and freed it.
template <class Loop>
struct frame {
	bench::waiter<Loop> w;
	const void *next = nullptr, *pprev = nullptr;
	int state = -1, value = -1;
	void freeze() noexcept {
		next = w.next;
		pprev = w.pprev;
		state = w.state.load();
		value = w.value;
	}
	bool unchanged() const noexcept { return next == w.next && pprev == w.pprev && state == w.state.load() && value == w.value; }
};

// What every run records, under one lock so the threaded test can share it.
struct ledger {
	std::mutex mutex;
	std::set<const void*> live;       // frames not yet freed
	std::vector<void*> quarantine;    // freed frames in an explored run, kept so a late access is seen, not undefined
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
	bool keep;  // freed frames stay allocated (quarantined) and are checked at the end
	std::atomic<int> resumed{0};

	scenario(Loop& a_, Loop& b_, Loop& c_, Loop& io_, bool naive_, bool keep_)
		: a(a_), b(b_), c(c_), io(io_), naive(naive_), keep(keep_) {}

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
			if (keep) {
				f->freeze();
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
	// Freed frames some waiter method wrote after the caller resumed.
	int touched_after_free() const {
		int n = 0;
		for (void* f : book.quarantine) { n += !static_cast<frame<Loop>*>(f)->unchanged(); }
		return n;
	}
	~scenario() {
		for (void* f : book.quarantine) { delete static_cast<frame<Loop>*>(f); }
	}
};

// Checks one finished run against the protocol and the cancellation table.
template <class Loop>
void verdict(scenario<Loop>& s, int callers) {
	CHECK(s.book.violations == 0);
	CHECK(s.touched_after_free() == 0);
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

// Picks every choice of a run: which loop steps next, and at each post to another loop whether
// the target runs it before the poster goes on. Replays `prefix`, then takes the first option,
// and records each choice so the next run can take the following one (depth-first).
struct chooser final : bench::manual_loop::scheduler {
	std::vector<int> prefix;
	std::vector<std::pair<int, int>> trace;  // (choice, options) per decision of this run
	int choose(int options) {
		const std::size_t i = trace.size();
		const int c = i < prefix.size() ? prefix[i] : 0;
		trace.emplace_back(c, options);
		return c;
	}
	bool run_now(bench::manual_loop&) override { return choose(2) == 1; }
	bool next() {  // the next schedule's prefix, or false when every schedule ran
		while (!trace.empty()) {
			const auto [c, n] = trace.back();
			trace.pop_back();
			if (c + 1 < n) {
				prefix.clear();
				for (const auto& t : trace) { prefix.push_back(t.first); }
				prefix.push_back(c + 1);
				return true;
			}
		}
		return false;
	}
};

// Runs one schedule to the end and checks it; returns whether a naive run broke the protocol.
bool replay(chooser& pick, int callers, bool naive, std::uint64_t& steps) {
	pick.trace.clear();
	bench::manual_loop a, b, c, io;
	scenario<bench::manual_loop> s(a, b, c, io, naive, true);
	s.deadline = std::make_unique<bench::wheel_deadline>(c.timers, 5 * ms);
	s.token = s.deadline->token();
	for (int id = 0; id < callers; ++id) { b.post([&s, id] { s.propose(id); }); }
	c.post([&] { c.timers.expire(10 * ms); });  // the deadline fires
	bench::manual_loop* loops[] = {&a, &b, &c, &io};
	for (auto* l : loops) { l->explorer = &pick; }
	for (;;) {
		bench::manual_loop* enabled[4];
		int n = 0;
		for (auto* l : loops) { if (!l->idle()) { enabled[n++] = l; } }
		if (!n) { break; }
		enabled[n > 1 ? pick.choose(n) : 0]->step();
		++steps;
	}
	for (auto* l : loops) { l->explorer = nullptr; }
	if (!naive) {
		verdict(s, callers);
		return false;
	}
	if (s.book.violations > 0 || s.touched_after_free() > 0) { return true; }
	for (int id = 0; id < callers; ++id) { if (s.book.resumes[id] != 1) { return true; } }
	return false;
}

void explore(int callers, bool naive) {
	chooser pick;
	std::uint64_t schedules = 0, steps = 0, broken = 0;
	do {
		broken += replay(pick, callers, naive, steps);
		++schedules;
	} while (pick.next());
	if (naive) {
		CHECK(broken > 0);
		std::printf("naive callers=%d schedules=%llu schedules_reaching_a_freed_frame=%llu (expected > 0) ok\n", callers,
			static_cast<unsigned long long>(schedules), static_cast<unsigned long long>(broken));
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
		scenario<bench::thread_loop> s(a, b, c, io, false, false);
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
