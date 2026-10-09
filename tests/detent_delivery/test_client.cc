#include "detent_delivery/client.hh"
#include "detent_distribution/messagepack.hh"
#include <asio.hpp>
#include <filesystem>
#include <iostream>
#include <source_location>
#include <thread>
namespace d = detent::distribution;
namespace t = detent::delivery;
namespace m = d::messagepack;
using namespace std::chrono_literals;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("delivery client invariant at " + std::to_string(where.line()));
}
d::Cursor cursor() {
	d::Cursor result;
	result.cluster.fill('c');
	result.group.fill('g');
	result.restore_epoch.fill('e');
	result.scope.fill('s');
	result.revision = 1;
	return result;
}
std::string response(bool compressed) {
	std::string out;
	m::array(out, 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	m::array(out, 5);
	const auto id = cursor();
	for (const auto *part : {&id.cluster, &id.group, &id.restore_epoch})
		m::string(out, {part->data(), part->size()}, true);
	m::string(out, {id.scope.data(), id.scope.size()}, true);
	m::unsigned_integer(out, 1);
	m::array(out, 4);
	for (unsigned i = 0; i < 4; ++i)
		m::unsigned_integer(out, 0);
	m::array(out, 0);
	m::array(out, 0);
	d::Identity update;
	update.fill('u');
	const auto body =
		d::encode_fragment({d::UpdateKind::Snapshot, update, 0, out.size(), true, out}, compressed);
	return "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.detent.distribution\r\nX-Detent-Profile: "
		   "1\r\nX-Detent-Revision: 1\r\nX-Detent-Kind: 1\r\nX-Detent-Coding: " +
		   std::string(compressed ? "zstd" : "raw") + "\r\nContent-Length: " + std::to_string(body.size()) +
		   "\r\n\r\n" + body;
}
struct Server {
	asio::io_context io;
	asio::ip::tcp::acceptor acceptor{io, {asio::ip::address_v4::loopback(), 0}};
	std::thread worker;
	std::exception_ptr failure;
	Server(std::shared_ptr<asio::ssl::context> context, unsigned mode, bool compressed) {
		worker = std::thread([&, context = std::move(context), mode, compressed] {
			try {
				auto accepted = acceptor.accept();
				if (mode == 1) {
					std::this_thread::sleep_for(100ms);
					return;
				}
				asio::ssl::stream<asio::ip::tcp::socket> socket(std::move(accepted), *context);
				asio::error_code error;
				socket.handshake(asio::ssl::stream_base::server, error);
				if (error)
					return; // Expected certificate/hostname rejection.
				asio::streambuf request;
				asio::read_until(socket, request, "\r\n\r\n", error);
				if (error)
					return;
				auto bytes = response(compressed);
				if (mode == 2)
					bytes.resize(bytes.find("\r\n\r\n") + 4 + 48);
				if (mode == 3)
					bytes.back() ^= 1;
				// Deliberately split TLS application writes independently of frames.
				for (std::size_t offset = 0; offset < bytes.size();) {
					const auto n = std::min<std::size_t>(31, bytes.size() - offset);
					asio::write(socket, asio::buffer(bytes.data() + offset, n), error);
					if (error)
						return;
					offset += n;
				}
				if (mode == 2)
					std::this_thread::sleep_for(100ms);
			} catch (...) {
				failure = std::current_exception();
			}
		});
	}
	~Server() { worker.join(); }
};
} // namespace
int main(int argc, char **argv) {
	try {
		check(argc == 2);
		const std::filesystem::path cert(argv[1]);
		auto server_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		detent::transport::configure_tls(
			*server_tls,
			{(cert / "one.pem").string(), (cert / "one.key").string(), (cert / "ca.pem").string()},
			detent::transport::TlsRole::PublisherServer);
		auto client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		detent::transport::configure_tls(*client_tls, {"", "", (cert / "ca.pem").string()},
										 detent::transport::TlsRole::PublisherClient);
		for (bool compressed : {false, true})
			for (unsigned mode = 0; mode < 5; ++mode) {
				auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
				{
					Server server(server_tls, mode == 4 ? 0 : mode, compressed);
					t::ClientOptions options;
					options.endpoint = server.acceptor.local_endpoint();
					options.hostname = mode == 4 ? "wrong.example" : "localhost";
					options.token = "0123456789abcdef0123456789abcdef";
					options.tls_context = client_tls;
					options.configured = cursor();
					options.compression = compressed;
					options.limits.storage_account = account;
					options.timeout = mode == 1 || mode == 2 ? 20ms : 2s;
					asio::io_context io;
					const auto began = std::chrono::steady_clock::now();
					auto pending = asio::co_spawn(io, t::receive(std::move(options)), asio::use_future);
					io.run();
					bool rejected = false;
					try {
						auto received = pending.get();
						check(mode == 0 && received.snapshot && received.snapshot->records().size() == 0 &&
							  received.target == cursor());
						for (;;) {
							d::WorkBudget work;
							if (received.snapshot->discard(work))
								break;
						}
					} catch (const std::exception &) {
						rejected = true;
					}
					check(rejected == (mode != 0));
					if (mode == 1 || mode == 2)
						check(std::chrono::steady_clock::now() - began < 500ms);
				}
				check(account->used() == 0);
			}
		std::cout << "incremental TLS client and deadline cleanup passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
