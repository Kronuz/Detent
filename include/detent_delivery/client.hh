#pragma once
#include "detent_transport/tls.hh"
#include "response.hh"
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <chrono>
namespace detent::delivery {
struct ClientOptions {
	asio::ip::tcp::endpoint endpoint;
	std::string hostname, token;
	// Prepared on the startup/configuration worker. Runtime requests never
	// reopen certificate files or relax chain/hostname verification.
	std::shared_ptr<asio::ssl::context> tls_context;
	distribution::Cursor configured;
	std::optional<distribution::Cursor> base;
	std::vector<std::string> namespaces;
	distribution::Limits limits;
	std::chrono::milliseconds timeout{60000};
	std::size_t request_bytes = 256 * 1024;
	bool compression = true;
};
// One absolute deadline covers connect, handshake, request and incremental
// reception. Cancellation reaps original network work and budgeted candidate
// disposal before returning. No complete response body is accumulated.
asio::awaitable<ReceivedUpdate> receive(ClientOptions);
} // namespace detent::delivery
