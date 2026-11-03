#pragma once
#include "task_asio.hh"
#include <asio/error_code.hpp>
#include <asio/system_error.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace detent::transport {
namespace detail {
template <typename Sig, typename Init> class checked_io;
template <typename... R, typename Init> class checked_io<void(asio::error_code, R...), Init> {
	static_assert(sizeof...(R) <= 1, "await_io returns at most one value");
	using Inner = decltype(task::await_asio<void(asio::error_code, R...)>(std::declval<Init>(),
																		   std::declval<task::cancel_token>()));

  public:
	checked_io(Init init, task::cancel_token token)
		: inner_(task::await_asio<void(asio::error_code, R...)>(std::move(init), std::move(token))) {}
	bool await_ready() { return inner_.await_ready(); }
	bool await_suspend(std::coroutine_handle<> handle) { return inner_.await_suspend(handle); }
	auto await_resume() {
		if constexpr (sizeof...(R) == 0) {
			if (const asio::error_code error = inner_.await_resume())
				throw asio::system_error(error);
		} else {
			auto result = inner_.await_resume();
			if (std::get<0>(result))
				throw asio::system_error(std::get<0>(result));
			return std::move(std::get<1>(result));
		}
	}

  private:
	Inner inner_;
};
} // namespace detail
// Awaits an Asio operation under a token and throws its error as asio::system_error. A requested
// token never starts the operation and throws operation_aborted. `Sig` is the completion signature
// (void(asio::error_code) or void(asio::error_code, T)); an operation that wants its error code
// back awaits task::await_asio instead.
template <typename Sig, typename Init>
[[nodiscard]] detail::checked_io<Sig, std::decay_t<Init>> await_io(Init &&init, task::cancel_token token = {}) {
	return detail::checked_io<Sig, std::decay_t<Init>>(std::forward<Init>(init), std::move(token));
}
// Awaits `timer` reaching its expiry; a cancel or an error throws asio::system_error.
[[nodiscard]] inline auto await_timer(asio::steady_timer &timer, task::cancel_token token = {}) {
	return await_io<void(asio::error_code)>([&timer](auto handler) { timer.async_wait(std::move(handler)); },
											std::move(token));
}
// Runs `work` on `scheduler` and the calling thread's `io` until it finishes, then returns its result or
// rethrows its exception. `io` must be the scheduler's own loop; call io.restart() before a second use.
template <typename T> T run_until_done(asio::io_context &io, task::asio_scheduler &scheduler, task::task<T> work) {
	std::exception_ptr failure;
	std::optional<T> value;
	task::spawn(task::scheduler_ref::of(scheduler), std::move(work), [&](std::exception_ptr error, std::optional<T> result) {
		failure = error;
		value = std::move(result);
	});
	io.run();
	if (failure)
		std::rethrow_exception(failure);
	return std::move(value.value());
}
inline void run_until_done(asio::io_context &io, task::asio_scheduler &scheduler, task::task<void> work) {
	std::exception_ptr failure;
	bool finished = false;
	task::spawn(task::scheduler_ref::of(scheduler), std::move(work), [&](std::exception_ptr error) {
		failure = error;
		finished = true;
	});
	io.run();
	if (failure)
		std::rethrow_exception(failure);
	if (!finished)
		throw std::logic_error("task did not finish");
}
} // namespace detent::transport
