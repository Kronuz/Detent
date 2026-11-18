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
//   close [callers]     review X2: a fifth loop closes the group at any point; close resolves
//                       waiters not yet admitted with `closed`, drains staged records, then frees
//                       the group's state, which nothing may touch after (a cancel racing close)
//   teardown [callers]  review X2: at any step every loop stops and phase 3 destroys unresumed
//                       frames; each waiter's destructor unlinks it, so the group, which outlives
//                       phase 3, names no destroyed frame
//   close-naive, teardown-naive   the controls: close frees the state with waiters linked; a
//                       destroyed waiter stays linked; each passes iff some schedule breaks
//   close-read [callers]  review R4: callers on B read instead (a read waits for its read index,
//                       then for the apply that reaches it); close resolves a read at either
//                       stage with `closed`, and a closing group confirms no read index; in both,
//                       caller 0 waits with no token, so only close can end its wait
//   close-open [callers]  review R4: callers waiting on one `create`/`recover` (the host's single
//                       flight, X15) whose durable step runs on the IO loop; close resolves them
//                       with `closed` once that step ends, never before
//   close-late [callers]  review R4: proposals, with the transport's `receive` and the
//                       application's `applied` (the Sink's Committed range, answered later
//                       through post_call, X10) reaching loop A at any point; both name the group
//                       by id, so a closing or closed group drops them and counts the drop
//   close-read-naive, close-open-naive, close-late-naive   the controls: close as at
//                       Detent:1ece2ff resolves only proposals not yet admitted; `receive` and
//                       `applied` reach the state through Group&; each passes iff some schedule breaks
//   x2 [callers]        close, teardown and their controls
//   r4 [callers]        close-read, close-open, close-late and their controls
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
// Committed reaches the sink, and the proposal's waiter completes if it still waits. close()
// (review X2) refuses new calls, resolves waiters not yet admitted with `closed`, lets staged
// records drain and their waiters complete, and frees the group's state once nothing of its own
// is queued. The table entry (closing, freed) outlives the state; any later touch of the state
// is a violation.
template <class Loop>
struct group_model {
	using waiter = bench::waiter<Loop>;
	Loop& loop;
	Loop& io;
	ledger& book;
	bool naive_close = false;  // the negative control: close frees the state with waiters linked
	bool naive_kinds = false;  // the control: close resolves only proposals not yet admitted
	bool naive_late = false;   // the control: receive and applied reach the state through Group&
	waiter* admission = nullptr;
	waiter* placed = nullptr;
	waiter* reads = nullptr;    // reads awaiting their read index or the apply that reaches it
	waiter* opening = nullptr;  // create/recover callers awaiting the current durable step
	Loop* application = nullptr;  // where the Sink hands Committed ranges (close-late)
	std::map<const waiter*, int> read_stage;
	int durable_steps = 0, received = 0, applied_calls = 0, applied_through = 0, dropped_receives = 0, dropped_applied = 0;
	std::map<const waiter*, int> record_of;
	std::set<int> staged, written, committed;
	int next_record = 1;
	int posted = 0;     // the group's own calls queued on its loop (its ready link); touched on A only
	int in_flight = 0;  // turn writes issued and not yet completed back on A; touched on A only
	bool closing = false, freed = false, open_started = false;
	waiter *admission_at_free = nullptr, *placed_at_free = nullptr;

	void enqueue(waiter& w, const task::cancel_token& token) {
		if (closing) {  // the table refuses a call to a closing or closed group
			w.close();
			return;
		}
		w.link(admission);
		w.watch(token);
		post(loop, [this] { turn(); });
	}
	void enqueue_read(waiter& w, const task::cancel_token& token) {
		if (closing) {
			w.close();
			return;
		}
		w.link(reads);
		w.watch(token);
		const waiter* key = &w;
		post(loop, [this, key] { read_step(key); });  // the read index, once a heartbeat round confirms it
	}
	void enqueue_open(waiter& w, const task::cancel_token& token) {
		if (closing) {
			w.close();
			return;
		}
		w.link(opening);
		w.watch(token);
		if (durable_steps) {  // already open: a later create/recover completes at once
			w.complete(0);
			return;
		}
		if (open_started) { return; }  // the host's single flight: later requests wait on this open (X15)
		open_started = true;
		++in_flight;  // the current durable step, machine-owned IO under the shutdown token (D8)
		io.post([this] {
			loop.post([this] {
				--in_flight;
				open_step();
			});
		});
	}
	// The transport's frame for this group, by id through the loop's group table (on A).
	void receive() {
		if (!naive_late && (closing || freed)) {
			++dropped_receives;
			return;
		}
		if (!live()) { return; }
		++received;
	}
	// The application's answer to a Committed range, by id through the group table (on A).
	void applied(int r) {
		if (!naive_late && (closing || freed)) {
			++dropped_applied;
			return;
		}
		if (!live()) { return; }
		++applied_calls;
		applied_through = std::max(applied_through, r);
	}
	void turn() {  // Core accepts the oldest proposal still waiting for admission
		if (!live()) { return; }
		waiter* w = admission;
		if (!w) { return maybe_free(); }  // its caller was cancelled before admission: never submitted
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
		++in_flight;  // the IO loop touches no group state: its completion posts back to A
		io.post([this, r] {
			loop.post([this, r] {
				--in_flight;
				barrier(r);
			});
		});
	}
	void barrier(int r) {
		if (!live()) { return; }
		written.insert(r);
		committed.insert(r);
		if (application) {  // the Sink hands the range over; the application answers later (X10)
			application->post([this, r] { loop.post([this, r] { applied(r); }); });
		}
		for (waiter* w = placed; w;) {
			if (!book.alive(w)) { return; }
			waiter* following = w->next;
			if (record_of[w] == r) {
				w->complete(r);
				break;
			}
			w = following;
		}
		maybe_free();
	}
	void close() {  // on A
		if (!live()) { return; }
		closing = true;
		if (naive_close) { return release(); }
		while (waiter* w = admission) {
			if (!book.alive(w)) { return; }
			w->close();  // unlinks, then resumes the caller if the claim wins
		}
		while (waiter* w = naive_kinds ? nullptr : reads) {  // a read at either stage holds no record
			if (!book.alive(w)) { return; }
			w->close();
		}
		maybe_free();  // an open waits for its durable step (open_step); staged records drain
	}
	// The state's lists still as they were when it was freed: nothing wrote into freed memory.
	bool untouched() const noexcept { return !freed || (admission == admission_at_free && placed == placed_at_free); }
	bool drained() const noexcept { return !admission && !placed && !reads && !opening; }

private:
	// Whether `key` is still in `head`'s list, reading only live waiters.
	bool listed(waiter* head, const waiter* key) {
		for (waiter* p = head; p; p = p->next) {
			if (!book.alive(p)) { return false; }
			if (p == key) { return true; }
		}
		return false;
	}
	void read_step(const waiter* key) {
		if (!live()) { return; }
		// A closing group sends no heartbeat, so no read index is confirmed, nor an apply reached.
		if (closing || !listed(reads, key)) { return maybe_free(); }
		auto* w = const_cast<waiter*>(key);
		if (++read_stage[key] == 1) {  // the read index arrived; the apply that reaches it is next
			post(loop, [this, key] { read_step(key); });
			return;
		}
		w->complete(0);
		maybe_free();
	}
	void open_step() {
		if (!live()) { return; }
		++durable_steps;
		while (waiter* w = closing && naive_kinds ? nullptr : opening) {
			if (!book.alive(w)) { return; }
			if (closing) {
				w->close();  // after its durable step, never before it
			} else {
				w->complete(0);
			}
		}
		maybe_free();
	}
	template <class F>
	void post(Loop& l, F f) {  // on A, to A
		++posted;
		l.post([this, f] {
			--posted;
			f();
		});
	}
	bool live() {
		if (!freed) { return true; }
		std::lock_guard lock(book.mutex);
		++book.violations;
		return false;
	}
	void maybe_free() {
		if (closing && !freed && drained() && posted == 0 && in_flight == 0) { release(); }
	}
	void release() {
		freed = true;
		admission_at_free = admission;
		placed_at_free = placed;
	}
};

// What a caller awaits: a proposal, a read, or a create/recover.
enum class call { propose, read, open };

// One run of `callers` calls sharing one fan-out deadline, on loops of type Loop.
template <class Loop>
struct scenario {
	Loop &a, &b, &c, &io;
	ledger book;
	group_model<Loop> group{a, io, book};
	std::unique_ptr<bench::wheel_deadline> deadline;
	task::cancel_token token;
	bool naive;
	bool keep;  // freed frames stay allocated (quarantined) and are checked at the end
	bool keep_linked = false;  // the negative control: a destroyed waiter stays in the group's list
	std::atomic<int> resumed{0};
	std::vector<frame<Loop>*> made;
	std::map<int, call> kind;  // caller -> what it awaits
	int receives = 0;          // frames the transport posted to the group's loop

	scenario(Loop& a_, Loop& b_, Loop& c_, Loop& io_, bool naive_, bool keep_)
		: a(a_), b(b_), c(c_), io(io_), naive(naive_), keep(keep_) {}

	void propose(int id, call k = call::propose, bool tokened = true) {  // on B
		kind[id] = k;
		auto* f = new frame<Loop>;
		{
			std::lock_guard lock(book.mutex);
			book.live.insert(&f->w);
		}
		f->w.caller = &b;
		f->w.group = &a;
		f->w.naive = naive;
		f->w.keep_linked = keep_linked;
		made.push_back(f);
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
		task::cancel_token t = tokened ? token : task::cancel_token{};
		a.post([this, f, t, k] {  // the call hops to the group's loop
			if (k == call::read) {
				group.enqueue_read(f->w, t);
			} else if (k == call::open) {
				group.enqueue_open(f->w, t);
			} else {
				group.enqueue(f->w, t);
			}
		});
	}
	// Freed frames some waiter method wrote after the caller resumed.
	int touched_after_free() const {
		int n = 0;
		for (void* f : book.quarantine) { n += !static_cast<frame<Loop>*>(f)->unchanged(); }
		return n;
	}
	// Task teardown's phase 3 (every loop joined, its queue gone): frames destroyed unresumed.
	// Returns how many were still linked in the group's lists.
	int teardown() {
		int linked = 0;
		for (frame<Loop>* f : made) {
			if (!book.live.count(&f->w)) { continue; }  // resumed: quarantined or freed already
			book.live.erase(&f->w);
			linked += f->w.linked();
			delete f;  // ~waiter deregisters its callback and unlinks it
		}
		return linked;
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
	CHECK(s.group.drained());
	for (int id = 0; id < callers; ++id) { CHECK(s.book.resumes[id] == 1); }
	std::set<int> values;
	for (int id = 0; id < callers; ++id) {
		const call k = s.kind.count(id) ? s.kind[id] : call::propose;
		if (s.book.outcome[id] == bench::waiter<Loop>::completed && k != call::propose) {
			CHECK(k != call::open || s.group.durable_steps == 1);  // opened once its durable step ended
		} else if (s.book.outcome[id] == bench::waiter<Loop>::closed && k == call::open) {
			CHECK(s.group.closing);  // refused at the call, or resolved by open_step after the durable step
		} else if (s.book.outcome[id] == bench::waiter<Loop>::completed) {
			CHECK(s.group.committed.count(s.book.value[id]));
			CHECK(values.insert(s.book.value[id]).second);
		} else if (s.book.outcome[id] == bench::waiter<Loop>::closed) {
			CHECK(s.group.closing);  // refused or resolved by close, never staged
		} else {
			CHECK(s.book.outcome[id] == bench::waiter<Loop>::cancelled && s.deadline->expired());
		}
	}
	// A staged record is written and committed whatever happened to its caller; a proposal
	// cancelled before admission was never staged, so staged records number the admitted ones.
	CHECK(s.group.staged == s.group.written && s.group.written == s.group.committed);
	CHECK(s.group.staged.size() <= std::size_t(callers));
	CHECK(s.group.untouched());
	CHECK(s.group.freed == s.group.closing);  // a closed group frees its state once drained
	CHECK(s.group.durable_steps == int(s.group.open_started));  // an open's durable step always ends
	// Every frame and every Committed range reached the group's table: delivered or dropped.
	CHECK(s.group.received + s.group.dropped_receives == s.receives);
	if (s.group.application) { CHECK(s.group.applied_calls + s.group.dropped_applied == int(s.group.committed.size())); }
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

// What a run adds to the base scenario (callers on B, the group on A, the deadline on C, the
// turn write on IO), and its negative control.
enum class mode {
	plain,           // review F3, F4, F14
	naive,           // control: cancel resumes the caller at once
	close,           // review X2: another loop, D, closes the group at any point of the run
	close_naive,     // control: close frees the group's state with waiters still linked
	teardown,        // review X2: at any step the loops stop and phase 3 destroys unresumed frames
	teardown_naive,  // control: a destroyed waiter leaves the group's list as is
	close_read,        // review R4: reads, closed at either stage
	close_read_naive,  // control: close resolves only proposals not yet admitted
	close_open,        // review R4: a create/recover, closed once its durable step ends
	close_open_naive,  // control: the same literal close leaves it linked
	close_late,        // review R4: receive and the application's applied reach A at any point
	close_late_naive,  // control: both through Group&, so a late one touches freed state
};
bool control(mode m) {
	return m == mode::naive || m == mode::close_naive || m == mode::teardown_naive || m == mode::close_read_naive || m == mode::close_open_naive ||
		m == mode::close_late_naive;
}
bool closes(mode m) { return m == mode::close || m == mode::close_naive || m >= mode::close_read; }

struct run_stats {
	std::uint64_t steps = 0, closed = 0, linked_at_teardown = 0, dropped_receives = 0, dropped_applied = 0;
};

// Runs one schedule to the end and checks it; returns whether a control run broke the protocol.
bool replay(chooser& pick, int callers, mode m, run_stats& stats) {
	pick.trace.clear();
	bench::manual_loop a, b, c, io, d, t;
	scenario<bench::manual_loop> s(a, b, c, io, m == mode::naive, true);
	s.keep_linked = m == mode::teardown_naive;
	s.group.naive_close = m == mode::close_naive;
	s.group.naive_kinds = m == mode::close_read_naive || m == mode::close_open_naive;
	s.group.naive_late = m == mode::close_late_naive;
	s.deadline = std::make_unique<bench::wheel_deadline>(c.timers, 5 * ms);
	s.token = s.deadline->token();
	const call k = m == mode::close_read || m == mode::close_read_naive ? call::read
		: m == mode::close_open || m == mode::close_open_naive          ? call::open
		                                                                : call::propose;
	// Reads and opens: caller 0 waits with no token, so only close can end its wait; the others
	// wait under the deadline, which races close.
	for (int id = 0; id < callers; ++id) { b.post([&s, id, k] { s.propose(id, k, k == call::propose || id > 0); }); }
	c.post([&] { c.timers.expire(10 * ms); });  // the deadline fires
	if (closes(m)) { d.post([&] { a.post([&] { s.group.close(); }); }); }  // a request on D drops the index
	if (m == mode::close_late || m == mode::close_late_naive) {  // T: the transport and the application
		s.group.application = &t;
		s.receives = 1;
		t.post([&] { a.post([&] { s.group.receive(); }); });
	}
	const bool tears = m == mode::teardown || m == mode::teardown_naive;
	bench::manual_loop* loops[] = {&a, &b, &c, &io, &d, &t};
	for (auto* l : loops) { l->explorer = &pick; }
	bool torn = false;
	for (;;) {
		bench::manual_loop* enabled[6];
		int n = 0;
		for (auto* l : loops) { if (!l->idle()) { enabled[n++] = l; } }
		if (!n) { break; }
		if (tears && pick.choose(2) == 1) {  // every loop joins here: what is queued never runs
			torn = true;
			break;
		}
		enabled[n > 1 ? pick.choose(n) : 0]->step();
		++stats.steps;
	}
	for (auto* l : loops) {
		l->explorer = nullptr;
		l->stop();
	}
	if (tears) {
		stats.linked_at_teardown += s.teardown();
		// The group outlives phase 3; its lists must not name a destroyed frame.
		const bool dangling = s.group.admission || s.group.placed;
		if (control(m)) { return dangling; }
		CHECK(!dangling && s.book.violations == 0 && s.touched_after_free() == 0);
		if (!torn) { verdict(s, callers); }
		return false;
	}
	for (int id = 0; id < callers; ++id) { stats.closed += s.book.outcome[id] == bench::waiter<bench::manual_loop>::closed; }
	stats.dropped_receives += s.group.dropped_receives;
	stats.dropped_applied += s.group.dropped_applied;
	if (!control(m)) {
		verdict(s, callers);
		return false;
	}
	if (s.book.violations > 0 || s.touched_after_free() > 0 || !s.group.untouched()) { return true; }
	for (int id = 0; id < callers; ++id) { if (s.book.resumes[id] != 1) { return true; } }
	return false;
}

const char* name(mode m) {
	static const char* names[] = {"explore", "naive", "close", "close-naive", "teardown", "teardown-naive", "close-read", "close-read-naive",
		"close-open", "close-open-naive", "close-late", "close-late-naive"};
	return names[int(m)];
}

void explore(int callers, mode m) {
	chooser pick;
	run_stats stats;
	std::uint64_t schedules = 0, broken = 0;
	do {
		broken += replay(pick, callers, m, stats);
		++schedules;
	} while (pick.next());
	if (control(m)) {
		CHECK(broken > 0);
		std::printf("%s callers=%d schedules=%llu schedules_breaking_the_protocol=%llu (expected > 0) ok\n", name(m), callers,
			static_cast<unsigned long long>(schedules), static_cast<unsigned long long>(broken));
		return;
	}
	if (m == mode::close || m == mode::close_read || m == mode::close_open) { CHECK(stats.closed > 0); }
	if (m == mode::close_late) { CHECK(stats.dropped_receives > 0 && stats.dropped_applied > 0); }
	if (m == mode::teardown) { CHECK(stats.linked_at_teardown > 0); }
	std::printf("%s callers=%d schedules=%llu steps=%llu closed_outcomes=%llu linked_at_teardown=%llu dropped_receives=%llu dropped_applied=%llu "
	            "violations=0 ok\n",
		name(m), callers, static_cast<unsigned long long>(schedules), static_cast<unsigned long long>(stats.steps),
		static_cast<unsigned long long>(stats.closed), static_cast<unsigned long long>(stats.linked_at_teardown),
		static_cast<unsigned long long>(stats.dropped_receives), static_cast<unsigned long long>(stats.dropped_applied));
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
	const auto counts = n ? std::vector<int>{n} : std::vector<int>{1, 2};
	for (mode m : {mode::plain, mode::naive, mode::close, mode::close_naive, mode::teardown, mode::teardown_naive, mode::close_read,
	         mode::close_read_naive, mode::close_open, mode::close_open_naive, mode::close_late, mode::close_late_naive}) {
		const bool r4 = m >= mode::close_read;
		const bool x2 = m != mode::plain && m != mode::naive && !r4;
		if (only == name(m) || only == "all" || (only == "x2" && x2) || (only == "r4" && r4)) {
			// One proposal beside the late calls keeps six loops tractable.
			const bool one = m == mode::close_late || m == mode::close_late_naive;
			for (int callers : one && !n ? std::vector<int>{1} : counts) { explore(callers, m); }
		}
	}
	if (only == "all" || only == "threads") { threads(n ? n : 3000); }
	std::printf("waiter_test: ok\n");
	return 0;
}
