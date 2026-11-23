// OwnerWake (include/detent/owner_loop.hh): a loop that sleeps on its timer with no near deadline is
// woken by work handed over from other threads, and never sleeps through any of it.
//   (a) a raise while the loop sleeps toward a one-hour deadline ends the sleep at once;
//   (b) a raise between begin_turn() and arm() makes arm() expire at once;
//   (c) four producers hand over 40,000 items, each followed by a raise; the loop, sleeping an hour
//       whenever its queue is empty, consumes every one. A lost wake leaves it asleep and fails the case.

#include "detent/owner_loop.hh"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto hour = std::chrono::hours(1);

void check(bool condition, const char *what) {
	if (!condition) {
		std::cerr << "owner wake: " << what << '\n';
		std::exit(1);
	}
}

void raise_ends_sleep() {
	detent::OwnerLoop owner;
	asio::steady_timer timer(owner.io());
	std::promise<void> armed, woke;
	asio::post(owner.io(), [&] {
		owner.wake().begin_turn();
		owner.wake().arm(timer, Clock::now() + hour);
		timer.async_wait([&](const asio::error_code &) { woke.set_value(); });
		armed.set_value();
	});
	armed.get_future().wait();
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	const auto raised = Clock::now();
	owner.wake().raise();
	check(woke.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready,
		  "(a) a raise did not end the sleep");
	std::cout << "(a) woke " << std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - raised).count()
			  << " us after the raise\n";
	asio::post(owner.io(), [&] { owner.wake().release(); });
	owner.stop();
}

void raise_before_arm() {
	detent::OwnerLoop owner;
	asio::steady_timer timer(owner.io());
	std::promise<void> woke;
	asio::post(owner.io(), [&] {
		owner.wake().begin_turn();
		owner.wake().raise(); // the work a turn missed, handed over before it sleeps
		owner.wake().arm(timer, Clock::now() + hour);
		timer.async_wait([&](const asio::error_code &) { woke.set_value(); });
	});
	check(woke.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready,
		  "(b) arm() after a raise slept");
	asio::post(owner.io(), [&] { owner.wake().release(); });
	owner.stop();
}

void producers_never_lost() {
	constexpr int producers = 4, items = 10000;
	detent::OwnerLoop owner;
	asio::steady_timer timer(owner.io());
	std::mutex mutex;
	std::deque<int> queue;
	int consumed = 0, turns = 0;
	std::promise<void> done;
	std::function<void()> turn = [&] {
		++turns;
		owner.wake().begin_turn();
		{
			std::lock_guard lock(mutex);
			consumed += static_cast<int>(queue.size());
			queue.clear();
		}
		if (consumed == producers * items) {
			owner.wake().release();
			done.set_value();
			return;
		}
		owner.wake().arm(timer, Clock::now() + hour);
		timer.async_wait([&](const asio::error_code &) { turn(); });
	};
	asio::post(owner.io(), turn);
	std::vector<std::thread> threads;
	for (int p = 0; p < producers; ++p)
		threads.emplace_back([&] {
			for (int i = 0; i < items; ++i) {
				{
					std::lock_guard lock(mutex);
					queue.push_back(i);
				}
				owner.wake().raise();
				if (i % 64 == 0)
					std::this_thread::yield();
			}
		});
	for (auto &thread : threads)
		thread.join();
	check(done.get_future().wait_for(std::chrono::seconds(30)) == std::future_status::ready,
		  "(c) the loop slept through a raise: items left unconsumed");
	std::cout << "(c) " << producers * items << " items in " << turns << " turns\n";
	owner.stop();
}
} // namespace

int main() {
	raise_ends_sleep();
	raise_before_arm();
	producers_never_lost();
	std::cout << "owner wake: no raise lost\n";
}
