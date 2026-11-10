// The blocking scheduler D5's steady-clock entry runs on (review X3), as a prototype to test:
// one waiting thread drives it; post and post_call are thread-safe and wake its wait; its timers
// are a wheel on the steady clock, touched only on the driving thread. task::inline_scheduler
// declares no post_call, so D8's cancel hop is dropped on it, and its queue is not thread-safe.
// The shape is Xapiand's engine::BlockingScheduler (src/engine/host.h:215-363) with the wheel.
#pragma once

#include "loops.hh"
#include "task.hh"

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace bench {

class blocking_scheduler {
public:
	blocking_scheduler() : start_(std::chrono::steady_clock::now()) {}
	blocking_scheduler(const blocking_scheduler&) = delete;
	blocking_scheduler& operator=(const blocking_scheduler&) = delete;

	// Any thread. Signals under the lock: once the driver sees its work it may finish and
	// destroy this scheduler, so no signal may follow the unlock.
	void post(std::coroutine_handle<> h) {
		std::lock_guard lock(mutex_);
		ready_.push_back({h, nullptr, nullptr});
		wake_.notify_one();
	}
	void post_call(void (*fn)(void*), void* arg) {
		std::lock_guard lock(mutex_);
		ready_.push_back({{}, fn, arg});
		wake_.notify_one();
	}
	bool runs_here() const noexcept { return std::this_thread::get_id() == driver_; }
	std::uint64_t now() const noexcept {
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start_).count());
	}
	wheel timers{0};  // on the driving thread only

	// The driving thread: runs queued work and due timers until done() holds, sleeping to the
	// wheel's next deadline or until a post. Returns false, without sleeping, when nothing is
	// queued, no timer is armed and `can_wake()` says no other thread can post: a lost wake.
	template <class Done, class CanWake>
	bool drive(Done&& done, CanWake&& can_wake) {
		driver_ = std::this_thread::get_id();
		std::unique_lock lock(mutex_);
		for (;;) {
			if (!ready_.empty()) {
				const ready r = ready_.front();
				ready_.pop_front();
				lock.unlock();
				{
					task::running_on scope(task::scheduler_ref::of(*this));
					if (r.fn) {
						r.fn(r.arg);
					} else {
						r.h.resume();
					}
				}
				lock.lock();
				continue;
			}
			lock.unlock();
			timers.expire(now());
			const bool finished = done();
			const std::uint64_t next = timers.next();
			const bool wakeable = can_wake();
			lock.lock();
			if (!ready_.empty()) { continue; }  // a fire, or another thread, queued work
			if (finished) { return true; }
			if (next == ~std::uint64_t{0}) {
				if (!wakeable) { return false; }
				wake_.wait(lock, [&] { return !ready_.empty(); });
			} else {
				wake_.wait_until(lock, start_ + std::chrono::nanoseconds(next), [&] { return !ready_.empty(); });
			}
		}
	}

private:
	struct ready {
		std::coroutine_handle<> h;
		void (*fn)(void*);
		void* arg;
	};
	const std::chrono::steady_clock::time_point start_;
	std::thread::id driver_{};
	std::mutex mutex_;
	std::condition_variable wake_;
	std::deque<ready> ready_;
};

}  // namespace bench
