// The blocking entry (docs/design/async-drivers.md, D5).
//   staged   review F5, on journal: an append stages its record into the core's TurnDriver and
//            completes only at that turn's barrier (store.h:328-333, turn_driver.h:24-26):
//            staged, it is neither done nor in flight, and a sync_wait over it with no one
//            running the TurnDriver blocks forever. Committing the turn inline completes it,
//            with no io_context, scheduler or extra thread.
//   rules    review F11, D5's loop over a model consensus group whose timer is always armed
//            (a leader re-arms its heartbeat, a follower its election, core.h:1003, 1054-1066):
//            the rule as designed returns, throws `stalled` or refuses `needs_peer` in bounded
//            steps and drains staged records first; the rule at 73e96d6 (throw only on an
//            empty wheel, return as soon as the task is done) is run as a control and must
//            hang or return with a record unwritten.
//   v1       the same cases under the rule at 73e96d6 alone: exits 1 at its first failure.
// Prints what each case observed; exits nonzero if an observation differs from the expected one.
#include "loops.hh"
#include "probe.hh"

#include "completion/posix.h"
#include "journal/store.h"
#include "journal/turn_driver.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace j = kronuz::journal;

namespace {
#define CHECK(c)                                                                     \
	do {                                                                             \
		if (!(c)) {                                                                  \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
			std::exit(1);                                                            \
		}                                                                            \
	} while (0)

const j::AdmissionLimits admission{{1024ull * 1024 * 1024, 65536}, {8ull * 1024 * 1024, 3}, {512ull * 1024 * 1024, 4}, 64, 3};
const j::TurnLogOptions turn_options{std::uint64_t{1} << 20, 32 << 10, 1, 4, 1 << 20};

fs::path directory(const fs::path& root, const std::string& name) {
	auto path = root / name;
	fs::create_directories(path);
	fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
	return path;
}
j::Identity identity(char tag) {
	j::Identity id{};
	id[0] = tag;
	return id;
}

// D5's loop over a model of the consensus driver on an inline scheduler, a manual clock, an
// inline turn log and two wheels: the groups' timers, and operation deadlines (task::deadline
// on the entry's scheduler, which the entry counts apart).
namespace rules {
constexpr std::uint64_t ms = 1'000'000, heartbeat = 100 * ms, election = 1'500 * ms;
constexpr int hang_bound = 100'000;  // advances of the clock with the task still waiting

struct entry;
struct group {
	entry& host;
	int voters;
	bool leader = false;
	std::uint64_t timer_at = 0;
	struct node : bench::wheel_entry { group* owner = nullptr; };
	node timer;  // election or heartbeat: armed after every step, as Core's are
	std::vector<std::function<void()>> admission;  // proposals waiting for a leader
	group(entry& h, int v, bool l);
	void rearm();
	void fired();
};

enum class outcome { placed, cancelled, needs_peer };

struct entry {
	bool designed;  // the rule D5 states; false: the rule at 73e96d6
	bench::manual_loop queue;
	bench::wheel groups{0}, operations{0};
	std::uint64_t now = 0;
	std::vector<int> staged, written, committed;
	std::vector<std::function<void()>> barriers;  // what each staged record's barrier completes
	int next_record = 1, advances = 0;
	std::vector<group*> owned;

	// A proposal (D8's rows 1-2): refused at the call when it needs a peer (D5); waits for a
	// leader; staged at once on a leader; a cancelled caller is released, a staged record stays.
	void propose(group& g, const task::cancel_token& token, std::function<void(outcome, int)> done) {
		if (designed && g.voters > 1) {
			queue.post([done] { done(outcome::needs_peer, 0); });
			return;
		}
		struct waiter {
			std::function<void(outcome, int)> done;
			bool released = false;
			std::optional<task::cancel_callback<std::function<void()>>> callback;
		};
		auto w = std::make_shared<waiter>();
		w->done = std::move(done);
		auto stage = [this, w] {
			const int r = next_record++;
			staged.push_back(r);
			barriers.push_back([this, w, r] {
				committed.push_back(r);
				if (!w->released) {
					w->released = true;
					queue.post([w, r] { w->done(outcome::placed, r); });
				}
			});
		};
		if (g.leader) {
			stage();
		} else {
			g.admission.push_back([w, stage] { if (!w->released) { stage(); } });
		}
		w->callback.emplace(token, std::function<void()>([this, w] {
			queue.post([w] {  // the hop to the group's loop, then the caller's resume
				if (w->released) { return; }
				w->released = true;
				w->done(outcome::cancelled, 0);
			});
		}));
	}
	void commit() {  // TurnDriver::commit over InlineCompletion: writes and syncs, then the barriers
		auto records = std::move(staged);
		auto done = std::move(barriers);
		staged.clear();
		barriers.clear();
		for (int r : records) { written.push_back(r); }
		for (auto& b : done) { b(); }
	}
	void advance(std::uint64_t to) {
		now = to;
		++advances;
		groups.expire(now);
		operations.expire(now);
	}
	// The next time that can move the task: an operation deadline, or the election of an owned
	// single voter that has no leader yet (it campaigns and wins alone, core.h:1003). A
	// heartbeat, or a multi-voter group's election, needs a peer to move anything.
	std::uint64_t next_progress() const {
		std::uint64_t t = operations.next();
		for (const group* g : owned) {
			if (g->voters == 1 && !g->leader && g->timer.armed()) { t = std::min(t, g->timer_at); }
		}
		return t;
	}

	struct result {
		std::string_view how;  // "returned", "stalled", "hang"
		int advances;
		std::uint64_t simulated_ms;
		std::size_t unwritten;
	};
	// Drives until `done()`: the task's frame finished.
	result run(const std::function<bool()>& done) {
		for (;;) {
			while (!queue.idle()) { queue.step(); }
			if (!designed) {  // D5 at 73e96d6
				if (done()) { return {"returned", advances, now / ms, staged.size()}; }
				if (!staged.empty()) {
					commit();
					continue;
				}
				const std::uint64_t t = std::min(groups.next(), operations.next());
				if (t == ~std::uint64_t{0}) { return {"stalled", advances, now / ms, staged.size()}; }
				if (advances == hang_bound) { return {"hang", advances, now / ms, staged.size()}; }
				advance(t);
				continue;
			}
			// D5: drain first (staged records are machine-owned IO, D8), then return; else move
			// the clock only to a time that can make progress; else the task can never finish.
			if (!staged.empty()) {
				commit();
				continue;
			}
			if (done()) { return {"returned", advances, now / ms, staged.size()}; }
			const std::uint64_t t = next_progress();
			if (t == ~std::uint64_t{0}) { return {"stalled", advances, now / ms, staged.size()}; }
			if (advances == hang_bound) { return {"hang", advances, now / ms, staged.size()}; }
			advance(t);
		}
	}
};

group::group(entry& h, int v, bool l) : host(h), voters(v), leader(l) {
	timer.owner = this;
	timer.fire = [](bench::wheel_entry& e) noexcept { static_cast<node&>(e).owner->fired(); };
	host.owned.push_back(this);
	rearm();
}
void group::rearm() {
	timer_at = host.now + (leader ? heartbeat : election);
	host.groups.arm(timer, timer_at);
}
void group::fired() {
	if (!leader && voters == 1) {  // campaigns and wins alone
		leader = true;
		auto waiting = std::move(admission);
		admission.clear();
		for (auto& admit : waiting) { admit(); }
	}
	rearm();  // a leader's heartbeat to no one, or a multi-voter group's next election
}

struct expectation {
	std::string_view name, how;
	std::optional<outcome> outcome_;
	int advances;  // -1: any
};

// One case under one rule: prints what it saw, and returns whether it matched `want`.
bool run_case(std::string_view name, bool designed, const expectation& want) {
	entry e{designed};
	std::optional<outcome> got;
	int record = 0;
	std::unique_ptr<group> g;
	std::unique_ptr<bench::wheel_deadline> deadline;
	std::optional<task::cancel_callback<std::function<void()>>> on_deadline;
	task::cancel_source sibling;
	auto finish = [&](outcome o, int r) {
		got = o;
		record = r;
	};
	if (name == "lost-wake") {  // a single-voter leader; the task awaits a wake nobody sends
		g = std::make_unique<group>(e, 1, true);
	} else if (name == "needs-peer") {  // a three-voter follower; the task proposes
		g = std::make_unique<group>(e, 3, false);
		e.queue.post([&] { e.propose(*g, {}, finish); });
	} else if (name == "first-election") {  // a single voter before its first election proposes
		g = std::make_unique<group>(e, 1, false);
		e.queue.post([&] { e.propose(*g, {}, finish); });
	} else if (name == "cancel-staged") {  // staged on a leader, then its caller is cancelled
		g = std::make_unique<group>(e, 1, true);
		e.queue.post([&] { e.propose(*g, sibling.token(), finish); });
		e.queue.post([&] { sibling.request(); });  // a sibling fails and cancels the fan-out
	} else if (name == "deadline") {  // the lost wake under a 30 s operation deadline
		g = std::make_unique<group>(e, 1, true);
		deadline = std::make_unique<bench::wheel_deadline>(e.operations, e.now + 30'000 * ms);
		on_deadline.emplace(deadline->token(), std::function<void()>([&] { e.queue.post([&] { finish(outcome::cancelled, 0); }); }));
	}
	const auto r = e.run([&] { return got.has_value(); });
	const bool unwritten = r.unwritten > 0 || e.written.size() != e.committed.size() || e.written.size() != std::size_t(e.next_record - 1);
	static const char* names[] = {"placed", "cancelled", "needs_peer"};
	std::printf("rule=%s case=%s how=%.*s outcome=%s advances=%d simulated_ms=%llu records=%d written=%zu committed=%zu unwritten=%d",
		designed ? "d5" : "d5-at-73e96d6", std::string(name).c_str(), int(r.how.size()), r.how.data(), got ? names[int(*got)] : "-",
		r.advances, static_cast<unsigned long long>(r.simulated_ms), e.next_record - 1, e.written.size(), e.committed.size(), int(unwritten));
	const bool ok = r.how == want.how && got == want.outcome_ && (want.advances < 0 || r.advances == want.advances) && !unwritten;
	std::printf(" %s\n", ok ? "ok" : "FAILS");
	(void)record;
	return ok;
}

// What the designed rule must do in each case.
const expectation designed_cases[] = {
	{"lost-wake", "stalled", std::nullopt, 0},
	{"needs-peer", "returned", outcome::needs_peer, 0},
	{"first-election", "returned", outcome::placed, 1},
	{"cancel-staged", "returned", outcome::cancelled, 0},
	{"deadline", "returned", outcome::cancelled, -1},
};
}  // namespace rules
}  // namespace

void staged() {
	const fs::path root = fs::path(std::getenv("BENCH_DIR") ? std::getenv("BENCH_DIR") : "bench-data") / "blocking-entry";
	fs::remove_all(root);
	fs::create_directories(root);
	const int threads = probe::threads();
	{
		auto log_io = std::make_shared<j::PosixIO>(directory(root, "log"));
		j::TurnDriver turns(*log_io, identity('L'), turn_options);
		auto io = std::make_shared<j::PosixIO>(directory(root, "g1"));
		j::Store store(io, turns, 1, admission);
		store.create(identity('J'));
		while (!store.accounting()) { store.inventory_step(128); }
		while (!turns.idle()) { turns.commit(); }
		CHECK(store.shared());

		const auto before = store.frontier().sequence;
		auto permit = store.reserve_append(j::AdmissionClass::Normal, 64);
		CHECK(permit);
		auto append = store.begin_append(std::move(*permit), "one record");
		std::printf("staged: done=%d in_flight=%d turn_pending=%d\n", append->done(), append->in_flight(), !turns.idle());
		CHECK(!append->done() && !append->in_flight() && !turns.idle());

		// The blocking entry's loop: when nothing else can run, commit the pending turn here.
		std::size_t commits = 0;
		while (!append->done() && !turns.idle()) {
			turns.commit();
			++commits;
		}
		const auto frontier = append->result();
		std::printf("turn committed inline: commits=%zu done=%d sequence=%llu->%llu threads_added=%d\n", commits, append->done(),
			static_cast<unsigned long long>(before), static_cast<unsigned long long>(frontier.sequence), probe::threads() - threads);
		CHECK(append->done() && frontier.sequence == before + 1 && probe::threads() == threads);
	}
	fs::remove_all(root);
}

int main(int argc, char** argv) {
	const std::string_view only = argc > 1 ? argv[1] : "all";
	if (only == "all" || only == "staged") { staged(); }
	if (only == "all" || only == "rules") {
		for (const auto& c : rules::designed_cases) { CHECK(rules::run_case(c.name, true, c)); }
		// The control: the rule at 73e96d6 hangs on the lost wake and the peer-needing proposal
		// (the group's timer always has an entry), and returns with the cancelled record unwritten.
		int failed = 0;
		for (const auto& c : rules::designed_cases) { failed += !rules::run_case(c.name, false, c); }
		CHECK(failed == 3);
		std::printf("control: the rule at 73e96d6 fails %d of 5 cases (expected 3) ok\n", failed);
	}
	if (only == "v1") {
		for (const auto& c : rules::designed_cases) {
			if (!rules::run_case(c.name, false, c)) { return 1; }
		}
	}
	std::printf("blocking_entry: ok\n");
	return 0;
}
