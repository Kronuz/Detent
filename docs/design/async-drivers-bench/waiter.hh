// The waiter protocol of docs/design/async-drivers.md D8, as a prototype to test: a caller on
// its loop awaits an operation of a group on the group's loop, and the operation's token may
// be requested on any thread. One claim word decides the outcome, so exactly one of
// completion, close and cancellation resumes the caller; the waiter leaves the group's list
// only on the group's loop, and the caller resumes only after that, so no list points into its
// frame. A frame destroyed unresumed (task teardown's phase 3, every loop joined) unlinks too.
#pragma once

#include "task.hh"

#include <atomic>
#include <functional>
#include <optional>

namespace bench {

template <class Loop>
struct waiter {
	enum outcome : int { pending = 0, completed = 1, cancelled = 2, closed = 3 };

	Loop* caller = nullptr;          // where resume runs
	Loop* group = nullptr;           // where the list lives
	std::function<void()> resume;    // runs on the caller's loop and may destroy the waiter
	bool naive = false;              // the negative control: resume posted straight to the caller
	bool keep_linked = false;        // the negative control: the destructor leaves the group's list as is
	std::atomic<int> state{pending};
	int value = 0;                   // written by the claim's winner before it posts resume
	waiter* next = nullptr;          // the group's list: linked, walked and unlinked on its loop only
	waiter** pprev = nullptr;

	struct on_cancel {
		waiter* self;
		void operator()() noexcept { self->cancel(); }
	};
	std::optional<task::cancel_callback<on_cancel>> callback;  // destroyed by the caller once resumed

	waiter() = default;
	waiter(const waiter&) = delete;
	waiter& operator=(const waiter&) = delete;
	// The caller resumed (nothing links it), or teardown destroys its frame with every loop joined.
	~waiter() {
		callback.reset();  // deregisters; waits for a callback running on another thread
		if (linked() && !keep_linked) { unlink(); }
	}

	bool linked() const noexcept { return pprev != nullptr; }
	void link(waiter*& head) noexcept {
		next = head;
		if (next) { next->pprev = &next; }
		head = this;
		pprev = &head;
	}
	void unlink() noexcept {
		*pprev = next;
		if (next) { next->pprev = pprev; }
		next = nullptr;
		pprev = nullptr;
	}
	// The group's loop, as it links the waiter: registers on the token (which may run cancel() now).
	void watch(const task::cancel_token& token) { callback.emplace(token, on_cancel{this}); }

	// The group's loop: the operation finished with v.
	void complete(int v) noexcept { finish(completed, v); }
	// The group's loop: the group closes (or its loop shuts down) with this waiter linked.
	void close() noexcept { finish(closed, 0); }
	// Any thread: the token was requested.
	void cancel() noexcept {
		if (!claim(cancelled)) { return; }  // complete() won and has resumed the caller
		if (naive) {
			caller->post(resume);
			return;
		}
		group->post([this] {
			if (linked()) { unlink(); }
			caller->post(resume);
		});
	}

private:
	void finish(outcome o, int v) noexcept {
		if (linked()) { unlink(); }
		if (!claim(o)) { return; }  // cancel() won; its hop to this loop resumes the caller
		value = v;
		caller->post(resume);
	}
	bool claim(outcome o) noexcept {
		int expected = pending;
		return state.compare_exchange_strong(expected, o, std::memory_order_acq_rel);
	}
};

}  // namespace bench
