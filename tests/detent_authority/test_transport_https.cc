#include "transport/https_client.hh"
#include "consensus/transport/tls.h"
#include <cassert>
#include <condition_variable>
#include <filesystem>
#include <http_asio_tls.h>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
struct App : http::HttpHandler {
	std::mutex mutex;
	std::condition_variable ready;
	std::unique_ptr<http::DeferredReply> pending;
	bool should_offload(const http::Request &) const override { return false; }
	void handle(const http::Request &request, http::ResponseWriter &writer) override {
		if (request.path == "/v1/health") {
			writer.send(200, "healthy");
			return;
		}
		if (request.path == "/fast") {
			writer.send(200, "fast");
			return;
		}
		http::DeferredOptions options;
		options.deadline = std::chrono::steady_clock::now() + 3s;
		std::lock_guard lock(mutex);
		pending = writer.defer(std::move(options));
		ready.notify_one();
	}
	std::unique_ptr<http::BodySink> on_request_body(http::Request &request) override {
		if (request.path == "/reject") {
			throw http::RequestRejected(503, "{\"error\":\"admission_full\"}", http::RejectionContent::Json);
		}
		return {};
	}
	std::unique_ptr<http::DeferredReply> take() {
		std::unique_lock lock(mutex);
		assert(ready.wait_for(lock, 2s, [&] { return bool(pending); }));
		return std::move(pending);
	}
};
int main(int argc, char **argv) {
	assert(argc == 2);
	std::filesystem::path cert(argv[1]);
	App app;
	auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
	reactor::tls::configure(
		*context, {(cert / "one.pem").string(), (cert / "one.key").string(), (cert / "ca.pem").string()},
		reactor::tls::Role::Server);
	asio::io_context io;
	asio::ip::tcp::acceptor reservation(io, {asio::ip::address_v4::loopback(), 0});
	auto endpoint = reservation.local_endpoint();
	reservation.close();
	http::HttpAsioService service(app, 1, 0, 16, http::tls_connection_factory_timed(context, 100ms), 4);
	http::AsioBindOptions bind;
	bind.address = "127.0.0.1";
	service.set_bind_options(bind);
	service.start(endpoint.port());
	asio::ssl::context client_context(asio::ssl::context::tls);
	reactor::tls::configure(client_context, {"", "", (cert / "ca.pem").string()},
									 reactor::tls::Role::Client);
	asio::ssl::stream<asio::ip::tcp::socket> client(io, client_context);
	reactor::tls::bind_hostname(client.native_handle(), "localhost");
	client.next_layer().connect(endpoint);
	client.handshake(asio::ssl::stream_base::client);
	asio::write(client, asio::buffer(std::string("GET /defer HTTP/1.1\r\nHost: localhost\r\n\r\n")));
	auto reply = app.take();
	// The monitor consumes this byte while the deferred response is pending.
	asio::write(client, asio::buffer("G", 1));
	std::this_thread::sleep_for(20ms);
	http::Response response;
	response.body = "deferred";
	assert(reply->complete(std::move(response)) == http::ReplySubmit::Accepted);
	asio::write(client, asio::buffer(std::string(
							"ET /fast HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")));
	std::string wire;
	std::array<char, 4096> bytes;
	asio::error_code error;
	for (;;) {
		auto size = client.read_some(asio::buffer(bytes), error);
		wire.append(bytes.data(), size);
		if (error)
			break;
	}
	assert(wire.find("deferred") != std::string::npos);
	assert(wire.find("fast") != std::string::npos);
	assert(wire.find("HTTP/1.1 200", wire.find("HTTP/1.1 200") + 1) != std::string::npos);
	const reactor::tls::Options client_options{"", "", (cert / "ca.pem").string()};
	assert(detent::transport::https_health(client_options, endpoint, "localhost") == "healthy");
	for (const auto &[hostname, bound] :
		 std::vector<std::pair<std::string, std::size_t>>{{"wrong.example", 65536}, {"localhost", 16}}) {
		bool rejected = false;
		try {
			detent::transport::https_health(client_options, endpoint, hostname, 2s, bound);
		} catch (const std::exception &) {
			rejected = true;
		}
		assert(rejected);
	}
	asio::ip::tcp::acceptor silent(io, {asio::ip::address_v4::loopback(), 0});
	bool timed_out = false;
	const auto began = std::chrono::steady_clock::now();
	try {
		detent::transport::https_health(client_options, silent.local_endpoint(), "localhost", 20ms);
	} catch (const std::exception &) {
		timed_out = true;
	}
	assert(timed_out && std::chrono::steady_clock::now() - began < 500ms);
	service.stop();
	asio::ip::tcp::acceptor expired_accept(io, {asio::ip::address_v4::loopback(), 0});
	asio::ip::tcp::socket expired_client(io);
	expired_client.connect(expired_accept.local_endpoint());
	auto expired_socket = expired_accept.accept();
	reactor::Reactor expired_owner(0, 0);
	http::AsioResponseOptions expired_options;
	bool expired_retired = false;
	std::exception_ptr expired_failure;
	auto expired_factory = http::tls_connection_factory_timed(context, 100ms);
	asio::co_spawn(io,
				   expired_factory(std::move(expired_socket), &app, &expired_owner, &expired_options,
								   reactor::SessionContext{std::chrono::steady_clock::now() - 200ms}),
				   [&](std::exception_ptr failure) {
					   expired_failure = failure;
					   expired_retired = true;
				   });
	io.restart();
	io.run();
	assert(expired_retired && !expired_failure);
	char unexpected_tls_byte = 0;
	auto expired_bytes = expired_client.read_some(asio::buffer(&unexpected_tls_byte, 1), error);
	assert(expired_bytes == 0 && error && "queue delay expires before TLS handshake allocation or output");
	asio::ip::tcp::acceptor capped_reservation(io, {asio::ip::address_v4::loopback(), 0});
	const auto capped_endpoint = capped_reservation.local_endpoint();
	capped_reservation.close();
	http::HttpAsioService capped(app, 1, 0, 16, http::tls_connection_factory(context, 100ms, 1, 1));
	capped.set_bind_options(bind);
	capped.start(capped_endpoint.port());
	asio::ssl::stream<asio::ip::tcp::socket> held(io, client_context);
	reactor::tls::bind_hostname(held.native_handle(), "localhost");
	held.next_layer().connect(capped_endpoint, error);
	assert(!error);
	held.handshake(asio::ssl::stream_base::client);
	bool full = false;
	try {
		detent::transport::https_health(client_options, capped_endpoint, "localhost");
	} catch (const std::exception &) {
		full = true;
	}
	assert(full && "idle authenticated connections must consume admission");
	held.next_layer().close();
	bool refunded = false;
	for (unsigned attempt = 0; attempt != 100 && !refunded; ++attempt) {
		try {
			refunded =
				detent::transport::https_health(client_options, capped_endpoint, "localhost") == "healthy";
		} catch (const std::exception &) {
		}
		std::this_thread::sleep_for(1ms);
	}
	assert(refunded);
	capped.stop();
	asio::ip::tcp::acceptor timed_reservation(io, {asio::ip::address_v4::loopback(), 0});
	const auto timed_endpoint = timed_reservation.local_endpoint();
	timed_reservation.close();
	http::HttpAsioService timed(app, 1, 0, 16, http::tls_connection_factory_timed(context, 100ms), 1);
	timed.set_request_timeout(40ms);
	timed.set_bind_options(bind);
	timed.start(timed_endpoint.port());
	for (const auto &prefix : std::vector<std::string>{
			 "", "GET /fast HTTP/1.1\r\nH",
			 "POST /fast HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1000\r\n\r\nx"}) {
		asio::ssl::stream<asio::ip::tcp::socket> slow(io, client_context);
		reactor::tls::bind_hostname(slow.native_handle(), "localhost");
		slow.next_layer().connect(timed_endpoint);
		slow.handshake(asio::ssl::stream_base::client);
		const auto started = std::chrono::steady_clock::now();
		if (!prefix.empty()) {
			asio::write(slow, asio::buffer(prefix));
		}
		char byte = 0;
		auto size = slow.read_some(asio::buffer(&byte, 1), error);
		assert(!size && error && std::chrono::steady_clock::now() - started < 500ms);
		for (unsigned attempt = 0; attempt < 100 && timed.connection_stats().active; ++attempt) {
			std::this_thread::sleep_for(1ms);
		}
		assert(timed.connection_stats().active == 0 &&
			   "incomplete headers and bodies refund original transport leases");
		assert(detent::transport::https_health(client_options, timed_endpoint, "localhost") == "healthy");
		for (unsigned attempt = 0; attempt < 100 && timed.connection_stats().active; ++attempt) {
			std::this_thread::sleep_for(1ms);
		}
	}
	{
		asio::ssl::stream<asio::ip::tcp::socket> rejected(io, client_context);
		reactor::tls::bind_hostname(rejected.native_handle(), "localhost");
		rejected.next_layer().connect(timed_endpoint);
		rejected.handshake(asio::ssl::stream_base::client);
		asio::write(rejected,
					asio::buffer(std::string(
						"POST /reject HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1000000\r\n\r\n")));
		std::string rejected_wire;
		for (;;) {
			auto size = rejected.read_some(asio::buffer(bytes), error);
			rejected_wire.append(bytes.data(), size);
			if (error) {
				break;
			}
		}
		assert(rejected_wire.find("HTTP/1.1 503") == 0 &&
			   rejected_wire.find("admission_full") != std::string::npos);
	}
	timed.stop();
	assert(timed.connection_stats().active == 0);
	std::cout << "HTTPS deferred reply preserves pipelined plaintext input\n";
}
