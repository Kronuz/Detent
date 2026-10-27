#pragma once
#include "reactor_tls.h"
#include <asio/use_future.hpp>
#include <http_client_tls.h>

namespace detent::transport {
namespace detail {
inline asio::awaitable<std::string> fetch_health(std::shared_ptr<asio::ssl::context> context,
                                                 asio::ip::tcp::endpoint endpoint, std::string hostname,
                                                 http::client::PreparedRequest request,
                                                 http::client::Deadline deadline, std::size_t maximum_bytes) {
	const auto executor = co_await asio::this_coro::executor;
	http::client::TlsLease connection({endpoint, hostname, std::move(context)}, executor);
	http::client::Limits limits;
	limits.body_bytes = maximum_bytes;
	limits.wire_bytes = maximum_bytes;
	auto response = co_await http::client::exchange(connection, std::move(request), deadline, limits);
	if (response.head.status != 200)
		throw std::runtime_error("HTTPS health endpoint failed");
	co_return std::string(response.body);
}
} // namespace detail
// CLI convenience; context preparation and OS work happen before the event loop runs.
inline std::string https_health(reactor::tls::Options options, asio::ip::tcp::endpoint endpoint,
                                std::string hostname,
                                std::chrono::milliseconds timeout = std::chrono::seconds(2),
                                std::size_t maximum_bytes = 65536) {
	const http::client::Deadline deadline(timeout);
	if (!maximum_bytes)
		throw std::invalid_argument("HTTPS health options");
	http::client::PreparedRequest request({"GET", hostname, "/v1/health", {}, {}, true});
	auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
	reactor::tls::configure(*context, options, reactor::tls::Role::Client);
	asio::io_context io;
	auto response = asio::co_spawn(io,
	                               detail::fetch_health(std::move(context), endpoint, std::move(hostname),
	                                                    std::move(request), deadline, maximum_bytes),
	                               asio::use_future);
	io.run();
	return response.get();
}
} // namespace detent::transport
