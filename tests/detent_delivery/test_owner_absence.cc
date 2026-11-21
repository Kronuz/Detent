// owner_absence (include/detent/owner_loop.hh): the time a loop was away from its work between two passes,
// which the authority compares with election_min to stop trusting frames that waited in its sockets.
// The pass and any lateness past a planned sleep count; the planned sleep does not.
//   (a) stopped mid-pass for 2.5 s, then a 1 ms sleep woken on time: 2.5 s away;
//   (b) stopped 2.5 s during a 1 ms sleep: 2.5 s away;
//   (c) a 250 ms planned sleep woken 1 ms late, with no stop: 2 ms away;
//   (d) a 250 ms planned sleep woken early by a raise: the pass alone;
//   (e) a pass that ended in a yield, not a sleep: the whole gap;
//   (f) 60 ms of stall in the pass and 60 ms late out of the sleep: 120 ms away;
//   (g) a deadline already past when the sleep began: no lateness before the sleep began.

#include "detent/owner_loop.hh"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace {
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::chrono::microseconds;

void expect(const char *what, Clock::duration got, Clock::duration want) {
	if (got != want) {
		std::cerr << "owner absence: " << what << ": "
				  << std::chrono::duration_cast<microseconds>(got).count() << " us, want "
				  << std::chrono::duration_cast<microseconds>(want).count() << " us\n";
		std::exit(1);
	}
}
} // namespace

int main() {
	const auto pass = Clock::now();
	{
		const auto slept = pass + milliseconds(2500);
		expect("(a) stopped mid-pass",
			   detent::owner_absence(pass, slept, slept + milliseconds(1), slept + milliseconds(1) + microseconds(100)),
			   milliseconds(2500) + microseconds(100));
	}
	{
		const auto slept = pass + milliseconds(1);
		expect("(b) stopped in the sleep",
			   detent::owner_absence(pass, slept, slept + milliseconds(1), slept + milliseconds(2501)),
			   milliseconds(1) + milliseconds(2500));
	}
	{
		const auto slept = pass + milliseconds(1);
		expect("(c) long planned sleep",
			   detent::owner_absence(pass, slept, slept + milliseconds(250), slept + milliseconds(251)), milliseconds(2));
	}
	{
		const auto slept = pass + milliseconds(1);
		expect("(d) woken early",
			   detent::owner_absence(pass, slept, slept + milliseconds(250), slept + milliseconds(40)), milliseconds(1));
	}
	{
		const auto next = pass + milliseconds(300);
		expect("(e) no sleep", detent::owner_absence(pass, next, next, next), milliseconds(300));
	}
	{
		const auto slept = pass + milliseconds(60);
		expect("(f) split stall",
			   detent::owner_absence(pass, slept, slept + milliseconds(1), slept + milliseconds(61)), milliseconds(120));
	}
	{
		const auto slept = pass + milliseconds(10);
		expect("(g) deadline already past",
			   detent::owner_absence(pass, slept, pass + milliseconds(5), slept + milliseconds(1)), milliseconds(11));
	}
	std::cout << "owner absence: the pass and the lateness count, the planned sleep does not\n";
}
