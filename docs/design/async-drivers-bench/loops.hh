// Loops for the protocol tests (wheel_test.cc, waiter_test.cc): a manual loop a test steps
// by hand, a thread loop with its own thread, and the deadline the design puts on a loop's
// wheel (D4, D8): a task::cancel_source requested with cancel_reason::deadline when its wheel
// entry fires, armed, cancelled and destroyed only on the wheel's loop.
#pragma once

#include "wheel.hh"
#include "task.hh"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace bench {

// A deadline on a loop's wheel, at nanoseconds on that loop's clock. Its destructor unlinks
// the entry on the same thread that fires it, so no expiry outlives it.
class wheel_deadline {
public:
	wheel_deadline(wheel& w, std::uint64_t at, const task::cancel_token& parent = {}) : wheel_(w), source_(parent) {
		entry_.source = &source_;
		entry_.fire = [](wheel_entry& e) noexcept { static_cast<node&>(e).source->request(task::cancel_reason::deadline); };
		wheel_.arm(entry_, at);
	}
	wheel_deadline(const wheel_deadline&) = delete;
	wheel_deadline& operator=(const wheel_deadline&) = delete;
	~wheel_deadline() { wheel_.cancel(entry_); }

	task::cancel_token token() const noexcept { return source_.token(); }
	bool expired() const noexcept { return source_.reason() == task::cancel_reason::deadline; }
	bool armed() const noexcept { return entry_.armed(); }

private:
	struct node : wheel_entry { task::cancel_source* source = nullptr; };
	wheel& wheel_;
	task::cancel_source source_;
	node entry_;
};

// A loop the test steps: post() queues, step() runs one call.
class manual_loop {
public:
	void post(std::function<void()> f) { queue_.push_back(std::move(f)); }
	bool idle() const noexcept { return queue_.empty(); }
	void step() {
		auto f = std::move(queue_.front());
		queue_.pop_front();
		f();
	}
	wheel timers;

private:
	std::deque<std::function<void()>> queue_;
};

// A loop with its own thread: post() from any thread, calls run in order on the loop's thread.
class thread_loop {
public:
	thread_loop() : thread_([this] { run(); }) {}
	thread_loop(const thread_loop&) = delete;
	thread_loop& operator=(const thread_loop&) = delete;
	~thread_loop() {
		{
			std::lock_guard lock(mutex_);
			stopping_ = true;
		}
		ready_.notify_one();
		thread_.join();
	}
	void post(std::function<void()> f) {
		{
			std::lock_guard lock(mutex_);
			queue_.push_back(std::move(f));
		}
		ready_.notify_one();
	}
	bool runs_here() const noexcept { return std::this_thread::get_id() == thread_.get_id(); }
	wheel timers;  // touched only on this loop's thread

private:
	void run() {
		std::unique_lock lock(mutex_);
		for (;;) {
			ready_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
			if (queue_.empty()) { return; }
			auto f = std::move(queue_.front());
			queue_.pop_front();
			lock.unlock();
			f();
			lock.lock();
		}
	}
	std::mutex mutex_;
	std::condition_variable ready_;
	std::deque<std::function<void()>> queue_;
	bool stopping_ = false;
	std::thread thread_;
};

}  // namespace bench
