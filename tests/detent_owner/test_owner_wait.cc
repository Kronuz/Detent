// T1 (docs/design/http-coroutines.md): the owner wait of D2 on a plain loop standing in for a reactor.
//
// A handler runs on `home`, hands a request to `owner` with detent::owner_submit, and waits there on the
// request's event. The cases:
//   (b) a stop request wakes the wait and the truthful reply comes back within 50 ms: stop while the
//       child waits, stop before the child reaches the owner, stop before the handler starts
//   (a) a completion settled before and after the child starts waiting
//   (c) a completion after the wait ended is clean
//   (d) a backstop expiry answers the backstop reply
//   plus a race of completions against a stop, and frames back to their baseline after each case.

#define TASK_COUNT_FRAMES 1

#include "detent/owner_loop.hh"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using clk = std::chrono::steady_clock;

static std::atomic<int> failures{0};
#define CHECK(cond)                                                                   \
	do {                                                                              \
		if (!(cond)) {                                                                \
			failures.fetch_add(1);                                                    \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                             \
	} while (0)

namespace {

constexpr auto far = 60s;  // a backstop no case reaches
constexpr auto wake_bound = 50ms;
constexpr auto give_up = 3s;  // how long a case waits for an outcome before it fails

// A one-shot gate for threads outside the loops.
class Gate {
public:
	void open() {
		{
			std::lock_guard g(m_);
			open_ = true;
		}
		cv_.notify_all();
	}
	bool wait(std::chrono::milliseconds limit) {
		std::unique_lock g(m_);
		return cv_.wait_for(g, limit, [&] { return open_; });
	}

private:
	std::mutex m_;
	std::condition_variable cv_;
	bool open_ = false;
};

// The per-request state the owner keeps; only the owner thread touches the fields but `done`.
struct Pending {
	task::event done;
	bool settled = false;
	int value = 0;
};

struct Reply {
	bool completed = false;  // the owner's completion, not a truthful reply
	int value = 0;
	task::cancel_reason reason = task::cancel_reason::none;
	bool on_owner = false;  // finish() ran on the owner's thread
	bool back_home = false;  // the handler resumed on home's thread
	clk::time_point at{};    // when the handler resumed
};

struct Fixture {
	detent::OwnerLoop home, owner;
	std::vector<std::shared_ptr<Pending>> held;  // owner thread only
	std::atomic<int> accepted{0};
	Gate accepted_one;

	// The D15 phases: stop all, close all, destroy all roots, then destroy.
	~Fixture() {
		home.stop();
		owner.stop();
		home.close();
		owner.close();
		home.destroy_roots();
		owner.destroy_roots();
	}

	// Runs on the owner thread: the request enters the owner's state.
	std::shared_ptr<Pending> accept(int settle_with) {
		auto p = std::make_shared<Pending>();
		if (settle_with) {  // the owner completes it before the child waits
			p->settled = true;
			p->value = settle_with;
			p->done.set();
		}
		held.push_back(p);
		if (accepted.fetch_add(1) == 0) { accepted_one.open(); }
		return p;
	}

	// The owner's completion of the n-th accepted request, run on the owner.
	std::future<void> complete(std::size_t n, int value) {
		auto ran = std::make_shared<std::promise<void>>();
		auto fut = ran->get_future();
		asio::post(owner.io(), [this, n, value, ran] {
			auto& p = held.at(n);
			if (!p->settled) {
				p->settled = true;
				p->value = value;
			}
			p->done.set();
			ran->set_value();
		});
		return fut;
	}

	// Holds the owner's thread until the returned gate opens.
	std::shared_ptr<Gate> block_owner() {
		auto release = std::make_shared<Gate>();
		auto running = std::make_shared<std::promise<void>>();
		auto up = running->get_future();
		asio::post(owner.io(), [release, running] {
			running->set_value();
			release->wait(30s);
		});
		up.wait();
		return release;
	}
};

// One handler: it runs on home and awaits the owner wait.
struct Outcome {
	std::promise<Reply> reply;
	std::future<Reply> got = reply.get_future();
	Gate started;
};

struct Request {
	int settle_with = 0;
	clk::time_point backstop_at = clk::now() + far;
	task::cancel_token stop;
};

task::task<void> handler(Fixture& f, std::shared_ptr<Outcome> out, Request req) {
	out->started.open();
	Reply r = co_await detent::owner_submit<Reply>(
		f.owner, [&f, req] { return f.accept(req.settle_with); },
		[&f](Pending& p, task::cancel_reason why) {
			Reply r;
			r.completed = p.settled;
			r.value = p.value;
			r.reason = why;
			r.on_owner = f.owner.inside();
			return r;
		},
		req.backstop_at, req.stop);
	r.back_home = f.home.inside();
	r.at = clk::now();
	out->reply.set_value(r);
}

std::shared_ptr<Outcome> start(Fixture& f, Request req) {
	auto out = std::make_shared<Outcome>();
	task::spawn(task::scheduler_ref::of(f.home.scheduler()), handler(f, out, std::move(req)));
	return out;
}

bool arrived(Outcome& o, Reply& r) {
	if (o.got.wait_for(give_up) != std::future_status::ready) { return false; }
	r = o.got.get();
	return true;
}

// Frames and orphaned waiters must be back to where they were once the fixture is gone.
template <typename F>
void each_case(const char* name, F body) {
	std::fprintf(stderr, "case %s\n", name);
	const auto frames = task::detail::live_frames();
	{
		Fixture f;
		body(f);
	}
	CHECK(task::detail::live_frames() == frames);
	CHECK(task::detail::orphaned_waiters() == 0);
}

// (b) a stop request wakes the wait.
void stop_while_waiting(Fixture& f) {
	task::cancel_source stop;
	auto out = start(f, {.stop = stop.token()});
	CHECK(f.accepted_one.wait(give_up));  // the child is on the owner and waiting
	const auto asked = clk::now();
	stop.request(task::cancel_reason::shutdown);
	Reply r;
	if (!arrived(*out, r)) {
		CHECK(!"the wait ended on the stop");
		return;
	}
	CHECK(r.at - asked < wake_bound);
	CHECK(!r.completed);
	CHECK(r.reason == task::cancel_reason::shutdown);
	CHECK(r.on_owner);
	CHECK(r.back_home);
}

void stop_before_the_child_reaches_the_owner(Fixture& f) {
	auto release = f.block_owner();
	task::cancel_source stop;
	auto out = start(f, {.stop = stop.token()});
	CHECK(out->started.wait(give_up));
	stop.request(task::cancel_reason::shutdown);
	CHECK(f.accepted.load() == 0);  // the owner is still held
	release->open();
	Reply r;
	if (!arrived(*out, r)) {
		CHECK(!"the wait ended on the stop");
		return;
	}
	CHECK(!r.completed);
	CHECK(r.reason == task::cancel_reason::shutdown);
	CHECK(r.on_owner);
	CHECK(r.back_home);
	CHECK(f.accepted.load() == 1);
}

void stop_before_the_handler_starts(Fixture& f) {
	task::cancel_source stop;
	stop.request(task::cancel_reason::shutdown);
	auto out = start(f, {.stop = stop.token()});
	Reply r;
	if (!arrived(*out, r)) {
		CHECK(!"the wait ended on the stop");
		return;
	}
	CHECK(!r.completed);
	CHECK(r.reason == task::cancel_reason::shutdown);
	CHECK(r.back_home);
}

// (a) a completion settled before the child waits, and after.
void completed_before_the_wait(Fixture& f) {
	task::cancel_source stop;
	auto out = start(f, {.settle_with = 7, .stop = stop.token()});
	Reply r;
	if (!arrived(*out, r)) {
		CHECK(!"the wait ended on the completion");
		return;
	}
	CHECK(r.completed);
	CHECK(r.value == 7);
	CHECK(r.reason == task::cancel_reason::none);
	CHECK(r.on_owner);
	CHECK(r.back_home);
}

void completed_after_the_wait_began(Fixture& f) {
	task::cancel_source stop;
	auto out = start(f, {.stop = stop.token()});
	CHECK(f.accepted_one.wait(give_up));
	CHECK(out->got.wait_for(20ms) == std::future_status::timeout);  // it is waiting
	f.complete(0, 11).wait();
	Reply r;
	if (!arrived(*out, r)) {
		CHECK(!"the wait ended on the completion");
		return;
	}
	CHECK(r.completed);
	CHECK(r.value == 11);
	CHECK(r.reason == task::cancel_reason::none);
	CHECK(r.on_owner);
	CHECK(r.back_home);
	stop.request(task::cancel_reason::shutdown);  // a stop after the reply changes nothing
}

// (c) a completion after the wait ended.
void completed_after_the_stop(Fixture& f) {
	task::cancel_source stop;
	auto out = start(f, {.stop = stop.token()});
	CHECK(f.accepted_one.wait(give_up));
	stop.request(task::cancel_reason::shutdown);
	Reply r;
	if (!arrived(*out, r)) {
		CHECK(!"the wait ended on the stop");
		return;
	}
	CHECK(!r.completed);
	f.complete(0, 5).wait();  // sets an event nobody waits on
	f.complete(0, 6).wait();  // twice: set() is idempotent
	CHECK(f.held.size() == 1);
}

// (d) a backstop expiry.
void backstop_expires(Fixture& f) {
	task::cancel_source stop;
	auto out = start(f, {.backstop_at = clk::now() + 30ms, .stop = stop.token()});
	Reply r;
	if (!arrived(*out, r)) {
		CHECK(!"the wait ended on the backstop");
		return;
	}
	CHECK(!r.completed);
	CHECK(r.reason == task::cancel_reason::deadline);
	CHECK(!stop.requested());  // the backstop does not stop the service
	CHECK(r.on_owner);
	CHECK(r.back_home);
	f.complete(0, 9).wait();  // late
}

void backstop_after_a_completion_changes_nothing(Fixture& f) {
	auto out = start(f, {.backstop_at = clk::now() + 150ms});
	CHECK(f.accepted_one.wait(give_up));
	f.complete(0, 3).wait();
	Reply r;
	if (!arrived(*out, r)) {
		CHECK(!"the wait ended on the completion");
		return;
	}
	CHECK(r.completed);
	CHECK(r.value == 3);
	CHECK(r.reason == task::cancel_reason::none);
	std::this_thread::sleep_for(250ms);  // the disarmed timer fires into nothing
}

// Completions race a stop on three threads: each request ends with one reply, completed or truthful.
void completions_race_a_stop(Fixture& f) {
	constexpr int n = 200;
	task::cancel_source stop;
	std::vector<std::shared_ptr<Outcome>> outs;
	for (int i = 0; i < n; ++i) { outs.push_back(start(f, {.stop = stop.token()})); }
	std::thread completer([&] {
		for (int i = 0; i < n; i += 2) {
			asio::post(f.owner.io(), [&f, i] {
				if (std::size_t(i) < f.held.size()) {
					auto& p = f.held[i];
					p->settled = true;
					p->value = i + 1;
					p->done.set();
				}
			});
		}
	});
	stop.request(task::cancel_reason::shutdown);
	completer.join();
	int completed = 0, truthful = 0;
	for (auto& o : outs) {
		Reply r;
		if (!arrived(*o, r)) {
			CHECK(!"every request got a reply");
			return;
		}
		CHECK(r.back_home);
		CHECK(r.on_owner);
		CHECK(r.reason != task::cancel_reason::none || r.completed);  // a stop after a settled request still answers it
		(r.completed ? completed : truthful)++;
	}
	CHECK(completed + truthful == n);
}

}  // namespace

int main() {
	task::cycles::calibrate();
	each_case("b: stop while the child waits", stop_while_waiting);
	each_case("b: stop before the child reaches the owner", stop_before_the_child_reaches_the_owner);
	each_case("b: stop before the handler starts", stop_before_the_handler_starts);
	each_case("a: completed before the wait", completed_before_the_wait);
	each_case("a: completed after the wait began", completed_after_the_wait_began);
	each_case("c: completed after the stop", completed_after_the_stop);
	each_case("d: backstop expires", backstop_expires);
	each_case("d: completed, then the backstop", backstop_after_a_completion_changes_nothing);
	each_case("race: completions against a stop", completions_race_a_stop);
	if (failures.load()) {
		std::fprintf(stderr, "%d check(s) failed\n", failures.load());
		return 1;
	}
	std::fprintf(stderr, "ok\n");
	return 0;
}
