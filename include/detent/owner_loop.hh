#pragma once

// The loop an owner of serialized work runs on, and the one shape a handler uses to hand it work
// (docs/design/http-coroutines.md D2).

#include "task_asio.hh"

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace detent {

// An io_context, a task scheduler on it, a work guard and one thread. It is not a reactor::Reactor:
// owners do blocking file I/O, and reactor::inside_run() stays false on them.
//
// Teardown is task's four phases (task cancellation.md D8), driven by the owner of every loop:
// stop() on all, close() on all, destroy_roots() on all, then destruction. The destructor repeats
// them, so a lone OwnerLoop needs no calls.
class OwnerLoop {
public:
	OwnerLoop() : scheduler_(io_.get_executor()), guard_(asio::make_work_guard(io_)), thread_([this] { io_.run(); }) {}
	~OwnerLoop() {
		stop();
		close();
		destroy_roots();
	}
	OwnerLoop(const OwnerLoop&) = delete;
	OwnerLoop& operator=(const OwnerLoop&) = delete;

	asio::io_context& io() noexcept { return io_; }
	task::asio_scheduler& scheduler() noexcept { return scheduler_; }
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
