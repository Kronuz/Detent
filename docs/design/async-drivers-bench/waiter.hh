// The waiter protocol of docs/design/async-drivers.md D8, as a prototype to test: a caller on
// its loop awaits an operation of a group on the group's loop, and the operation's token may
// be requested on any thread. One claim word decides the outcome, so exactly one of
// completion and cancellation resumes the caller; the waiter leaves the group's list only on
// the group's loop, and the caller resumes only after that, so no list points into its frame.
#pragma once

#include "task.hh"

#include <atomic>
#include <functional>
#include <optional>

namespace bench {

template <class Loop>
struct waiter {
	enum outcome : int { pending = 0, completed = 1, cancelled = 2 };

	Loop* caller = nullptr;          // where resume runs
	Loop* group = nullptr;           // where the list lives
	std::function<void()> resume;    // runs on the caller's loop and may destroy the waiter
	bool naive = false;              // the negative control: resume posted straight to the caller
	std::atomic<int> state{pending};
	int value = 0;                   // written by the claim's winner before it posts resume
	waiter* next = nullptr;          // the group's list: linked, walked and unlinked on its loop only
	waiter** pprev = nullptr;

	struct on_cancel {
		waiter* self;
		void operator()() noexcept { self->cancel(); }
	};
	std::optional<task::cancel_callback<on_cancel>> callback;  // destroyed by the caller once resumed

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
	void complete(int v) noexcept {
		if (linked()) { unlink(); }
		if (!claim(completed)) { return; }  // cancel() won; its hop to this loop resumes the caller
		value = v;
		caller->post(resume);
	}
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
	bool claim(outcome o) noexcept {
		int expected = pending;
		return state.compare_exchange_strong(expected, o, std::memory_order_acq_rel);
	}
};

}  // namespace bench
