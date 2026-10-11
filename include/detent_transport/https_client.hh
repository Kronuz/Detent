#pragma once
#include "detent_transport/tls.hh"
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/use_future.hpp>
#include <http_parser.h>
#include <variant>

namespace detent::transport {
namespace detail {
inline asio::awaitable<std::string> fetch_health(TlsOptions options, asio::ip::tcp::endpoint endpoint,
												 std::string hostname, std::size_t maximum_bytes) {
	auto executor = co_await asio::this_coro::executor;
	asio::ssl::context context(asio::ssl::context::tls);
	configure_tls(context, options, TlsRole::PublisherClient);
	asio::ssl::stream<asio::ip::tcp::socket> stream(executor, context);
	configure_publisher_client(stream.native_handle(), hostname);
	co_await stream.next_layer().async_connect(endpoint, asio::use_awaitable);
	co_await stream.async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);
	const std::string request =
		"GET /v1/health HTTP/1.1\r\nHost: " + hostname + "\r\nConnection: close\r\n\r\n";
	co_await asio::async_write(stream, asio::buffer(request), asio::use_awaitable);
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
		const auto size = co_await stream.async_read_some(asio::buffer(bytes), asio::use_awaitable);
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
struct HealthResult {
	std::variant<std::string, std::exception_ptr> value;
};
inline asio::awaitable<HealthResult> health_result(TlsOptions options, asio::ip::tcp::endpoint endpoint,
												   std::string hostname, std::size_t maximum_bytes) {
	try {
		co_return HealthResult{
			co_await fetch_health(std::move(options), endpoint, std::move(hostname), maximum_bytes)};
	} catch (...) {
		co_return HealthResult{std::current_exception()};
	}
}
inline asio::awaitable<std::string>
health_before_deadline(TlsOptions options, asio::ip::tcp::endpoint endpoint, std::string hostname,
					   std::chrono::milliseconds timeout, std::size_t maximum_bytes) {
	auto executor = co_await asio::this_coro::executor;
	asio::steady_timer timer(executor, timeout);
	asio::error_code timer_error;
	using namespace asio::experimental::awaitable_operators;
	auto result = co_await (health_result(std::move(options), endpoint, std::move(hostname), maximum_bytes) ||
							timer.async_wait(asio::redirect_error(asio::use_awaitable, timer_error)));
	if (result.index() != 0)
		throw std::runtime_error("HTTPS health deadline");
	auto reply = std::move(std::get<0>(result).value);
	if (auto error = std::get_if<std::exception_ptr>(&reply))
		std::rethrow_exception(*error);
	co_return std::move(std::get<std::string>(reply));
}
} // namespace detail
// CLI convenience: all network operations have one absolute asynchronous deadline.
inline std::string https_health(TlsOptions options, asio::ip::tcp::endpoint endpoint, std::string hostname,
								std::chrono::milliseconds timeout = std::chrono::seconds(2),
								std::size_t maximum_bytes = 65536) {
	if (timeout <= std::chrono::milliseconds::zero() || !maximum_bytes ||
		hostname.find_first_of("\r\n") != std::string::npos)
		throw std::invalid_argument("HTTPS health options");
	asio::io_context io;
	auto response =
		asio::co_spawn(io,
					   detail::health_before_deadline(std::move(options), endpoint, std::move(hostname),
													  timeout, maximum_bytes),
					   asio::use_future);
	io.run();
	return response.get();
}
} // namespace detent::transport
