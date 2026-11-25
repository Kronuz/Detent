#pragma once

// D12: tests drive a handler the way a connection does. The request goes through create_extension and
// the body intake, then handle() runs as a task on a reactor's scheduler that the calling thread runs, so
// reactor::inside_run() is true and D14's assertions fire as on a real loop. D15's phases follow.

#include <http_handler.h>
#include <http_owned_stream.h>
#include <reactor.h>

#include <asio/executor_work_guard.hpp>
#include <asio/steady_timer.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace detent_test {

// A writer that records a buffered reply and also offers the owned-stream capability; wire() plays the
// transport for an owned stream.
class Writer : public http::ResponseWriter {
  public:
	int code = 0;
	std::string body;
	std::vector<std::pair<std::string, std::string>> headers;
	bool ended = false;
	std::shared_ptr<http::detail::OwnedStreamExchange> exchange;
	std::shared_ptr<std::pmr::memory_resource> resource;
	std::weak_ptr<void> admission; // what the stream retains until it settles

	void status(int c) override { code = c; }
	void set_header(std::string_view name, std::string_view value) override {
		headers.emplace_back(std::string(name), std::string(value));
	}
	void write(std::string_view s) override { body += s; }
	void end() override { ended = true; }
	void set_close() override {}
	http::OwnedStreamCapability stream(http::StreamOptions requested) override {
		admission = requested.reply.request_retention;
		auto pool = requested.resource ? requested.resource : resource;
		exchange = std::allocate_shared<http::detail::OwnedStreamExchange>(
			std::pmr::polymorphic_allocator<http::detail::OwnedStreamExchange>(pool ? pool.get() : std::pmr::get_default_resource()),
			std::move(requested), [] {});
		return http::detail::make_resource_unique<http::OwnedStreamReply>(pool, exchange);
	}

	// Plays the transport for the owned stream: writes every chunk, then settles. Fills code, body and the
	// head of a reply that left through the stream. Returns false if it did not settle.
	bool wire(http::ReplySettlement outcome = http::ReplySettlement::Written) {
		if (!exchange)
			return true;
		std::string head;
		for (unsigned turn = 0; turn < 100000; ++turn) {
			if (auto original = exchange->take_write()) {
				head.append(original->head);
				body.append(original->chunk.bytes);
				exchange->complete_write(original->serial, outcome);
			}
			if (exchange->settle()) {
				if (head.size() > 12 && head.compare(0, 9, "HTTP/1.1 ") == 0)
					code = std::atoi(head.c_str() + 9);
				wire_head = std::move(head);
				return true;
			}
		}
		return false;
	}
	std::string wire_head;

	// One reply write taken from the owned stream and kept unsettled, as a stalled transport does.
	struct Held {
		int status = 0;
		std::string body;
		std::uint64_t serial = 0;
	};
	std::optional<Held> pull() {
		if (!exchange)
			return std::nullopt;
		auto original = exchange->take_write();
		if (!original)
			return std::nullopt;
		Held held;
		held.serial = original->serial;
		held.body.assign(original->chunk.bytes);
		if (original->head.size() > 12)
			held.status = std::atoi(std::string(original->head.substr(9, 3)).c_str());
		return held;
	}
	// Ends the write taken by pull() with `outcome` and settles the exchange.
	bool release(const Held &held, http::ReplySettlement outcome = http::ReplySettlement::Written) {
		exchange->complete_write(held.serial, outcome);
		return exchange->settle();
	}
};

// create_extension and the body intake, as the connection's headers-complete hook does. `body` is fed to
// the sink, or buffered when the handler wants no sink.
inline void intake(http::HttpHandler &handler, http::Request &request, std::string_view body = {}) {
	request.extension = handler.create_extension(request);
	if (auto sink = handler.on_request_body(request)) {
		sink->write(body);
		sink->end();
	} else {
		request.body.assign(body);
	}
}

// Runs handle() on a fresh reactor loop, to completion, then D15's phases. Rethrows what handle() threw.
inline void run_handle(http::HttpHandler &handler, http::Request &request, http::ResponseWriter &writer,
					   std::chrono::seconds limit = std::chrono::seconds(60)) {
	reactor::Reactor loop;
	request.set_scheduler(&loop.scheduler());
	auto guard = asio::make_work_guard(loop.io());
	std::exception_ptr failure;
	bool finished = false;
	task::spawn(task::scheduler_ref::of(loop.scheduler()), handler.handle(request, writer), [&](std::exception_ptr error) {
		failure = error;
		finished = true;
		guard.reset();
		loop.io().stop();
	});
	asio::steady_timer watchdog(loop.io(), limit);
	watchdog.async_wait([&](const asio::error_code &error) {
		if (!error) {
			guard.reset();
			loop.io().stop();
		}
	});
	loop.run();
	if (!finished) {
		std::fprintf(stderr, "run_handle: handler did not finish within %lld s\n", static_cast<long long>(limit.count()));
		std::abort();
	}
	watchdog.cancel();
	loop.scheduler().close();
	loop.scheduler().destroy_roots();
	if (failure)
		std::rethrow_exception(failure);
}

// intake() then run_handle(), then plays the owned stream if the handler used one.
inline void run_handler(http::HttpHandler &handler, http::Request &request, Writer &writer, std::string_view body = {}) {
	intake(handler, request, body);
	run_handle(handler, request, writer);
	writer.wire();
}

// A request on its own thread and loop, like another connection: for a handler that is expected to wait.
class Async {
  public:
	Async(http::HttpHandler &handler, http::Request &request, Writer &writer, std::string_view body = {}) {
		intake(handler, request, body);
		thread_ = std::thread([this, &handler, &request, &writer] {
			try {
				run_handle(handler, request, writer);
			} catch (...) {
				error_ = std::current_exception();
			}
			finished_.store(true, std::memory_order_release);
		});
	}
	~Async() { join(); }
	// True once the handler has returned (or thrown); finish() then does not wait.
	bool finished() const { return finished_.load(std::memory_order_acquire); }
	void join() {
		if (thread_.joinable())
			thread_.join();
	}
	void finish() {
		join();
		if (error_)
			std::rethrow_exception(error_);
	}

  private:
	std::thread thread_;
	std::exception_ptr error_;
	std::atomic<bool> finished_{false};
};

} // namespace detent_test
