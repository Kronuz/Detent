// The timer wheel the design proposes (docs/design/async-drivers.md, D4), as a prototype to
// measure: owner-thread only, clock-agnostic (deadlines are nanoseconds on the caller's
// clock), intrusive (a 24-byte entry inside the group, no allocation), O(1) arm and cancel,
// and next() to arm the one reactor timer. 4,096 slots of 1 ms; a later deadline waits in
// its slot and is skipped until due.
#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace bench {

struct wheel_entry {
	wheel_entry* next = nullptr;
	wheel_entry** pprev = nullptr;  // the pointer that points here: a slot head or a predecessor's next
	std::uint64_t deadline = 0;
	bool armed() const noexcept { return pprev != nullptr; }
};

class wheel {
public:
	static constexpr std::uint64_t tick = 1'000'000;  // 1 ms
	static constexpr std::size_t slots = 4096, mask = slots - 1, words = slots / 64;

	explicit wheel(std::uint64_t now = 0) noexcept : cursor_(now / tick) {}

	void arm(wheel_entry& e, std::uint64_t deadline) noexcept {
		if (e.armed()) { unlink(e); }
		e.deadline = deadline;
		const std::uint64_t at = deadline / tick < cursor_ ? cursor_ : deadline / tick;
		const std::size_t s = at & mask;
		e.next = head_[s];
		if (e.next) { e.next->pprev = &e.next; }
		head_[s] = &e;
		e.pprev = &head_[s];
		bits_[s / 64] |= std::uint64_t{1} << (s % 64);
		++count_;
	}
	void cancel(wheel_entry& e) noexcept {
		if (e.armed()) { unlink(e); }
	}
	// Runs fire(entry) for every entry due at `now`, in slot order; fire may re-arm it.
	template <class F>
	std::size_t expire(std::uint64_t now, F&& fire) {
		std::size_t fired = 0;
		const std::uint64_t last = now / tick;
		std::uint64_t steps = last - cursor_ + 1;
		if (last < cursor_) { return 0; }
		if (steps > slots) { steps = slots; }
		for (std::uint64_t k = 0; k < steps; ++k) {
			const std::size_t s = (cursor_ + k) & mask;
			if (!(bits_[s / 64] & (std::uint64_t{1} << (s % 64)))) { continue; }
			for (wheel_entry* e = head_[s]; e;) {
				wheel_entry* following = e->next;
				if (e->deadline <= now) { unlink(*e); ++fired; fire(*e); }
				e = following;
			}
		}
		cursor_ = last;
		return fired;
	}
	// The end of the next occupied slot: every entry filed there this round is due by then
	// (an entry a whole round later costs one early wake per round); ~0 when empty.
	std::uint64_t next() const noexcept {
		if (!count_) { return ~std::uint64_t{0}; }
		const std::size_t start = cursor_ & mask;
		for (std::size_t k = 0; k <= words; ++k) {
			const std::size_t w = (start / 64 + k) % words;
			std::uint64_t word = bits_[w];
			if (k == 0) { word &= ~std::uint64_t{0} << (start % 64); }
			if (word) {
				const std::size_t s = w * 64 + static_cast<std::size_t>(std::countr_zero(word));
				const std::uint64_t distance = (s - start) & mask;
				return (cursor_ + distance + 1) * tick;
			}
		}
		return ~std::uint64_t{0};
	}
	std::size_t size() const noexcept { return count_; }

private:
	void unlink(wheel_entry& e) noexcept {
		*e.pprev = e.next;
		if (e.next) { e.next->pprev = e.pprev; }
		auto** first = head_.data();
		if (e.pprev >= first && e.pprev < first + slots && !*e.pprev) {
			const std::size_t s = static_cast<std::size_t>(e.pprev - first);
			bits_[s / 64] &= ~(std::uint64_t{1} << (s % 64));
		}
		e.next = nullptr; e.pprev = nullptr;
		--count_;
	}

	std::array<wheel_entry*, slots> head_{};
	std::array<std::uint64_t, words> bits_{};
	std::uint64_t cursor_;
	std::size_t count_ = 0;
};

}  // namespace bench
