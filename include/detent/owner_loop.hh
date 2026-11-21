#pragma once

// The loop an owner of serialized work runs on, and the one shape a handler uses to hand it work
// (docs/design/http-coroutines.md D2).

#include "task_asio.hh"

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace detent {

// How a loop on the owner sleeps without polling: it waits on its timer until its next real deadline, and
// any thread that hands it work raises the wake, which ends the wait. The loop calls begin_turn() before
// reading any work and arm() when a turn leaves nothing runnable, so no raise is lost: one after
// begin_turn() either makes arm() expire at once or posts a cancel that ends the wait. Copies share it.
class OwnerWake {
public:
	explicit OwnerWake(asio::any_io_executor executor) : state_(std::make_shared<State>()), executor_(std::move(executor)) {}

	// Any thread, once the work it hands over is visible to the owner.
	void raise() const noexcept {
		if (state_->raised.exchange(true, std::memory_order_acq_rel)) { return; }
		try {
			asio::post(executor_, [state = state_] {
				if (state->sleeper) { state->sleeper->cancel(); }
			});
		} catch (...) {
			// The flag stays raised: the next arm() expires at once; only a wait already armed ends at its deadline.
		}
	}
	// Owner thread, at the start of a turn: true when a raise came since the last call.
	bool begin_turn() noexcept { return state_->raised.exchange(false, std::memory_order_acq_rel); }
	// Owner thread: arms `timer` for `at`, or for now when a raise came since begin_turn(). The caller waits on it.
	void arm(asio::steady_timer& timer, std::chrono::steady_clock::time_point at) {
		state_->sleeper = &timer;
		timer.expires_at(state_->raised.load(std::memory_order_acquire) ? std::chrono::steady_clock::time_point{} : at);
	}
	// Owner thread: forgets the timer before it is destroyed; later raises only set the flag.
	void release() noexcept { state_->sleeper = nullptr; }

private:
	struct State {
		std::atomic<bool> raised{false};
		asio::steady_timer* sleeper = nullptr;  // owner thread only
	};
	std::shared_ptr<State> state_;
	asio::any_io_executor executor_;
};

// How long a loop was away from its work between the pass that began at `pass` and the next one at `next`:
// the pass up to `slept`, when it began to sleep, plus any lateness past `planned`, the deadline of that
// sleep. The planned sleep is not an absence. A pass that ended without a sleep passes `next` as both.
inline std::chrono::steady_clock::duration owner_absence(std::chrono::steady_clock::time_point pass, std::chrono::steady_clock::time_point slept,
														 std::chrono::steady_clock::time_point planned, std::chrono::steady_clock::time_point next) noexcept {
	const auto late = next - std::max(planned, slept);
	return (slept - pass) + std::max(late, std::chrono::steady_clock::duration::zero());
}

// An io_context, a task scheduler on it, the wake of the loop the owner runs on it, a work guard and one
// thread. It is not a reactor::Reactor: owners do blocking file I/O, and reactor::inside_run() stays
// false on them.
//
// Teardown is task's four phases (task cancellation.md D8), driven by the owner of every loop:
// stop() on all, close() on all, destroy_roots() on all, then destruction. The destructor repeats
// them, so a lone OwnerLoop needs no calls.
class OwnerLoop {
public:
	OwnerLoop() : scheduler_(io_.get_executor()), wake_(io_.get_executor()), guard_(asio::make_work_guard(io_)), thread_([this] { io_.run(); }) {}
	~OwnerLoop() {
		stop();
		close();
		destroy_roots();
	}
	OwnerLoop(const OwnerLoop&) = delete;
	OwnerLoop& operator=(const OwnerLoop&) = delete;

	asio::io_context& io() noexcept { return io_; }
	task::asio_scheduler& scheduler() noexcept { return scheduler_; }
	// The wake of the owner's loop: producers raise it on any thread; the loop arms its timer with it.
	OwnerWake& wake() noexcept { return wake_; }
	bool inside() const noexcept { return scheduler_.runs_here(); }

	// Phase 1: release the work guard, stop the loop and join its thread. Queued handlers are dropped.
	void stop() {
		guard_.reset();
		io_.stop();
		if (thread_.joinable()) { thread_.join(); }
	}
	// Phase 2: the scheduler drops what is posted to it and destroys what is spawned on it.
	void close() noexcept { scheduler_.close(); }
	// Phase 3: destroys every task still suspended on this loop. The io_context outlives it.
	void destroy_roots() noexcept { scheduler_.destroy_roots(); }

private:
	asio::io_context io_;
	task::asio_scheduler scheduler_;
	OwnerWake wake_;
	asio::executor_work_guard<asio::io_context::executor_type> guard_;
	std::thread thread_;
};

// A handler's owner wait (D2). The handler awaits this child: it moves to `owner`, calls accept() there,
// waits on the event `done` of the per-request state accept() returns, and ends at the first of
//   - `done.set()` (the owner's completion),
//   - `stop` requested (a shutdown token the service owns),
//   - `backstop_at` (a deadline linked to `stop`),
// then returns finish(state, reason) from the owner. `reason` is none when the wait ended on `done` alone,
// otherwise the reason the stop or the backstop requested. When the child ends, task posts the handler back
// to its own loop, so only the owner thread touches the state.
//
// `State` has `task::event done`; the owner keeps it alive through the shared_ptr, so a completion after the
// wait ended sets an event nobody waits on.
template <typename Reply, typename Accept, typename Finish>
task::task<Reply> owner_submit(OwnerLoop& owner, Accept accept, Finish finish, std::chrono::steady_clock::time_point backstop_at, task::cancel_token stop) {
	co_await task::schedule_on{task::scheduler_ref::of(owner.scheduler())};
	auto state = accept();
	task::deadline backstop(owner.scheduler(), backstop_at, stop);
	task::cancel_callback wake(backstop.token(), [&done = state->done] { done.set(); });
	co_await state->done;
	co_return finish(*state, backstop.token().reason());
}

}  // namespace detent
