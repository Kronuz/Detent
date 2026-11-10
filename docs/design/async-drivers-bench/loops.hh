// Loops for the protocol tests (wheel_test.cc, waiter_test.cc): a manual loop a test steps
// by hand, a thread loop with its own thread, and the deadline the design puts on a loop's
// wheel (D4, D8): a task::cancel_source requested with cancel_reason::deadline when its wheel
// entry fires. The entry is touched only on the wheel's loop; the deadline itself keeps task's
// contract and may be created and destroyed on any thread (review F13).
#pragma once

#include "wheel.hh"
#include "task.hh"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace bench {

// A loop the test steps: post() queues, step() runs one call. An explorer may make each post
// from another loop a scheduling point: when run_now() says so, this loop runs everything queued
// up to the new call before post() returns, as its own thread could before the poster goes on.
class manual_loop {
public:
	struct scheduler {
		virtual bool run_now(manual_loop& target) = 0;
	};
	scheduler* explorer = nullptr;

	void post(std::function<void()> f) {
		queue_.push_back(std::move(f));
		if (explorer && !running_ && explorer->run_now(*this)) {
			for (std::size_t n = queue_.size(); n; --n) { step(); }
		}
	}
	bool idle() const noexcept { return queue_.empty(); }
	void step() {
		auto f = std::move(queue_.front());
		queue_.pop_front();
		running_ = true;
		f();
		running_ = false;
	}
	wheel timers;

private:
	std::deque<std::function<void()>> queue_;
	bool running_ = false;  // a call of this loop is on the stack: posts to it only queue
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

// A deadline on a loop's wheel, at nanoseconds on that loop's clock. Its entry lives in a state
// the wheel holds a reference to while armed, so only the loop touches the entry: on the loop,
// creation arms and destruction unlinks at once; on another thread (`home` set) each is posted
// to the loop. Once the destructor returns the deadline requests nothing, as task's does.
class wheel_deadline {
	enum : int { live, firing, fired, disarmed };
	struct state : wheel_entry {
		std::atomic<int> refs{1};  // the deadline's; the wheel's while armed; one per post in flight
		std::atomic<int> phase{live};
		task::cancel_source source;
		wheel& timers;
		state(wheel& w, const task::cancel_token& parent) : source(parent), timers(w) { ++alive; }
		~state() { --alive; }
		void release() noexcept {
			if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) { delete this; }
		}
		static void arm(state* s) {  // on the loop
			if (s->phase.load(std::memory_order_acquire) != live) { return; }
			s->refs.fetch_add(1, std::memory_order_relaxed);
			s->timers.arm(*s, s->deadline);
		}
		static void unlink(state* s) {  // on the loop
			if (!s->armed()) { return; }
			s->timers.cancel(*s);
			s->release();
		}
	};

public:
	static inline std::atomic<int> alive{0};  // states not yet freed, for the tests

	wheel_deadline(wheel& w, std::uint64_t at, const task::cancel_token& parent = {}, thread_loop* home = nullptr)
		: home_(home), s_(new state(w, parent)) {
		s_->deadline = at;
		s_->fire = [](wheel_entry& e) noexcept {
			auto& s = static_cast<state&>(e);
			int expected = live;
			if (s.phase.compare_exchange_strong(expected, firing, std::memory_order_acq_rel)) {
				s.source.request(task::cancel_reason::deadline);
				s.phase.store(fired, std::memory_order_release);
				s.phase.notify_all();
			}
			s.release();  // the wheel's
		};
		if (here()) {
			state::arm(s_);
		} else {
			s_->refs.fetch_add(1, std::memory_order_relaxed);
			home_->post([s = s_] { state::arm(s); s->release(); });
		}
	}
	wheel_deadline(const wheel_deadline&) = delete;
	wheel_deadline& operator=(const wheel_deadline&) = delete;
	~wheel_deadline() {
		int expected = live;
		if (!s_->phase.compare_exchange_strong(expected, disarmed, std::memory_order_acq_rel) && !here()) {
			while (s_->phase.load(std::memory_order_acquire) == firing) { s_->phase.wait(firing, std::memory_order_acquire); }
		}
		if (here()) {
			state::unlink(s_);
		} else {
			s_->refs.fetch_add(1, std::memory_order_relaxed);
			home_->post([s = s_] { state::unlink(s); s->release(); });
		}
		s_->release();
	}

	task::cancel_token token() const noexcept { return s_->source.token(); }
	bool expired() const noexcept { return s_->source.reason() == task::cancel_reason::deadline; }
	bool armed() const noexcept { return s_->armed(); }

private:
	bool here() const noexcept { return !home_ || home_->runs_here(); }  // no home: a single-threaded wheel
	thread_loop* home_;
	state* s_;
};

}  // namespace bench
