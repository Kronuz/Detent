#include "transport/tls.hh"
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
	detent::transport::configure_tls(
		*context, {(cert / "one.pem").string(), (cert / "one.key").string(), (cert / "ca.pem").string()},
		detent::transport::TlsRole::PublisherServer);
	asio::io_context io;
	asio::ip::tcp::acceptor reservation(io, {asio::ip::address_v4::loopback(), 0});
	auto endpoint = reservation.local_endpoint();
	reservation.close();
	http::HttpAsioService service(app, 1, 0, 16, http::tls_connection_factory(context, 100ms));
	http::AsioBindOptions bind;
	bind.address = "127.0.0.1";
	service.set_bind_options(bind);
	service.start(endpoint.port());
	asio::ssl::context client_context(asio::ssl::context::tls);
	detent::transport::configure_tls(client_context, {"", "", (cert / "ca.pem").string()},
									 detent::transport::TlsRole::PublisherClient);
	asio::ssl::stream<asio::ip::tcp::socket> client(io, client_context);
	detent::transport::configure_publisher_client(client.native_handle(), "localhost");
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
	service.stop();
	std::cout << "HTTPS deferred reply preserves pipelined plaintext input\n";
}
