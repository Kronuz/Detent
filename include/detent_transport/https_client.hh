#pragma once
#include "detent_transport/await_io.hh"
#include "reactor_tls.h"
#include <asio.hpp>
#include <http_parser.h>

namespace detent::transport {
namespace detail {
inline task::task<std::string> fetch_health(task::asio_scheduler &scheduler, reactor::tls::Options options,
											asio::ip::tcp::endpoint endpoint, std::string hostname,
											std::size_t maximum_bytes, task::cancel_token token) {
	asio::ssl::context context(asio::ssl::context::tls);
	reactor::tls::configure(context, options, reactor::tls::Role::Client);
	asio::ssl::stream<asio::ip::tcp::socket> stream(scheduler.executor(), context);
	reactor::tls::bind_hostname(stream.native_handle(), hostname);
	co_await await_io<void(asio::error_code)>(
		[&](auto handler) { stream.next_layer().async_connect(endpoint, std::move(handler)); }, token);
	co_await await_io<void(asio::error_code)>(
		[&](auto handler) { stream.async_handshake(asio::ssl::stream_base::client, std::move(handler)); }, token);
	const std::string request =
		"GET /v1/health HTTP/1.1\r\nHost: " + hostname + "\r\nConnection: close\r\n\r\n";
	co_await await_io<void(asio::error_code, std::size_t)>(
		[&](auto handler) { asio::async_write(stream, asio::buffer(request), std::move(handler)); }, token);
	struct Response {
		std::string body;
		std::size_t maximum;
		bool complete = false;
	} response{{}, maximum_bytes};
	http_parser parser;
	http_parser_init(&parser, HTTP_RESPONSE);
	parser.data = &response;
	http_parser_settings settings{};
	settings.on_body = [](http_parser *p, const char *bytes, std::size_t size) {
		auto &result = *static_cast<Response *>(p->data);
		if (size > result.maximum - result.body.size())
			return 1;
		try {
			result.body.append(bytes, size);
		} catch (...) {
			return 1;
		}
		return 0;
	};
	settings.on_message_complete = [](http_parser *p) {
		static_cast<Response *>(p->data)->complete = true;
		http_parser_pause(p, 1);
		return 0;
	};
	std::array<char, 4096> bytes;
	std::size_t received = 0;
	while (!response.complete) {
		const auto size = co_await await_io<void(asio::error_code, std::size_t)>(
			[&](auto handler) { stream.async_read_some(asio::buffer(bytes), std::move(handler)); }, token);
		if (!size || size > maximum_bytes - received)
			throw std::runtime_error("HTTPS response bound");
		received += size;
		const auto consumed = http_parser_execute(&parser, &settings, bytes.data(), size);
		if (consumed != size ||
			(HTTP_PARSER_ERRNO(&parser) != HPE_OK && HTTP_PARSER_ERRNO(&parser) != HPE_PAUSED))
			throw std::runtime_error("HTTPS response framing");
	}
	if (parser.status_code != 200)
		throw std::runtime_error("HTTPS health endpoint failed");
	co_return response.body;
}
inline task::task<std::string>
health_before_deadline(task::asio_scheduler &scheduler, reactor::tls::Options options,
					   asio::ip::tcp::endpoint endpoint, std::string hostname, std::chrono::milliseconds timeout,
					   std::size_t maximum_bytes) {
	task::deadline deadline(scheduler, std::chrono::steady_clock::now() + timeout);
	try {
		co_return co_await fetch_health(scheduler, std::move(options), endpoint, std::move(hostname),
										maximum_bytes, deadline.token());
	} catch (...) {
		if (deadline.expired())
			throw std::runtime_error("HTTPS health deadline");
		throw;
	}
}
} // namespace detail
// CLI convenience: all network operations have one absolute asynchronous deadline.
inline std::string https_health(reactor::tls::Options options, asio::ip::tcp::endpoint endpoint, std::string hostname,
								std::chrono::milliseconds timeout = std::chrono::seconds(2),
								std::size_t maximum_bytes = 65536) {
	if (timeout <= std::chrono::milliseconds::zero() || !maximum_bytes ||
		hostname.find_first_of("\r\n") != std::string::npos)
		throw std::invalid_argument("HTTPS health options");
	asio::io_context io;
	task::asio_scheduler scheduler(io.get_executor());
	return run_until_done(io, scheduler,
						  detail::health_before_deadline(scheduler, std::move(options), endpoint, std::move(hostname),
														 timeout, maximum_bytes));
}
} // namespace detent::transport
