#pragma once
#include "detent_transport/await_io.hh"
#include <asio/steady_timer.hpp>
#include <chrono>

// Test support: suspends the calling task on its loop for `length`.
inline task::task<void> pause(task::asio_scheduler &scheduler, std::chrono::milliseconds length) {
	asio::steady_timer timer(scheduler.executor(), length);
	co_await task::await_asio<void(asio::error_code)>([&](auto handler) { timer.async_wait(std::move(handler)); });
}
