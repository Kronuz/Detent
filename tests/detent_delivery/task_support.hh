#pragma once
#include "detent_transport/await_io.hh"
#include <asio/steady_timer.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <optional>

// Test support: suspends the calling task on its loop for `length`.
inline task::task<void> pause(task::asio_scheduler &scheduler, std::chrono::milliseconds length) {
	asio::steady_timer timer(scheduler.executor(), length);
	co_await task::await_asio<void(asio::error_code)>([&](auto handler) { timer.async_wait(std::move(handler)); });
}

// Test support: starts `work` on `scheduler` and returns what it will produce; the caller runs the loop.
template <typename T> std::future<T> spawn_future(task::asio_scheduler &scheduler, task::task<T> work) {
	auto promise = std::make_shared<std::promise<T>>();
	auto future = promise->get_future();
	task::spawn(task::scheduler_ref::of(scheduler), std::move(work),
				[promise](std::exception_ptr error, std::optional<T> value) {
					if (error)
						promise->set_exception(error);
					else
						promise->set_value(std::move(*value));
				});
	return future;
}
inline std::future<void> spawn_future(task::asio_scheduler &scheduler, task::task<void> work) {
	auto promise = std::make_shared<std::promise<void>>();
	auto future = promise->get_future();
	task::spawn(task::scheduler_ref::of(scheduler), std::move(work), [promise](std::exception_ptr error) {
		if (error)
			promise->set_exception(error);
		else
			promise->set_value();
	});
	return future;
}
