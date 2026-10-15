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
				if (mode >= 5 && mode <= 10) {
					constexpr const char *errors[] = {"source_not_leader", "unauthorized",
													  "admission_full",	   "source_behind",
													  "outcome_unknown",   "forbidden"};
					const auto status = mode == 6 ? 401 : mode == 10 ? 403 : 503;
					const auto body = "{\"error\":\"" + std::string(errors[mode - 5]) + "\"}";
					bytes = "HTTP/1.1 " + std::to_string(status) +
							" Control\r\nContent-Type: application/json\r\nContent-Length: " +
							std::to_string(body.size()) + "\r\n\r\n" + body;
				}

				if (mode == 2 || mode == 11)
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

struct SessionServer {
	asio::io_context io;
	asio::ip::tcp::acceptor acceptor{io, {asio::ip::address_v4::loopback(), 0}};
	std::thread worker;
	std::exception_ptr failure;
	SessionServer(std::shared_ptr<asio::ssl::context> context, bool compressed) {
		worker = std::thread([&, context = std::move(context), compressed] {
			try {
				for (unsigned step = 0; step < 6; ++step) {
					asio::ssl::stream<asio::ip::tcp::socket> socket(acceptor.accept(), *context);
					socket.handshake(asio::ssl::stream_base::server);
					asio::streambuf input;
					asio::read_until(socket, input, "\r\n\r\n");
					std::istream stream(&input);
					std::string line, headers;
					std::size_t length = 0;
					while (std::getline(stream, line) && line != "\r") {
						headers += line + '\n';
						if (line.starts_with("Content-Length: "))
							length = std::stoul(line.substr(16));
					}
					if (input.size() < length)
						asio::read(socket, input, asio::transfer_exactly(length - input.size()));
					std::string body(length, '\0');
					stream.read(body.data(), body.size());
					check(headers.find("/session/") != std::string::npos);
					std::string wire;
					if (step == 0) {
						check(headers.starts_with("POST /v2/distribution/session/open "));
						check(body.find("\"base\":") != std::string::npos);
						check(body.find("\"revision\":1}") != std::string::npos);
						const std::string control =
							"{\"version\":1,\"session\":\"11111111111111111111111111111111\","
							"\"window\":0,\"mode\":\"snapshot\",\"wait_ms\":1000}";
						wire = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
							   std::to_string(control.size()) + "\r\n\r\n" + control;
					} else if (step == 1) {
						check(headers.starts_with("POST /v2/distribution/session/next "));
						check(body.find("\"after\":0") != std::string::npos);
						wire = response(compressed);
						wire.insert(wire.find("\r\n") + 2, "X-Detent-Session-Version: 1\r\nX-Detent-Session: "
														   "11111111111111111111111111111111\r\n"
														   "X-Detent-Window: 1\r\n");
					} else if (step == 2) {
						check(body.find("\"after\":1") != std::string::npos &&
							  body.find("\"published\":{") != std::string::npos);
						wire = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
					} else if (step == 3 || step == 4) {
						check(body.find("\"after\":1") != std::string::npos &&
							  body.find("\"published\":{") != std::string::npos);
						const std::string control =
							step == 3 ? "{\"error\":\"source_behind\"}" : "{\"error\":\"session_expired\"}";
						wire = std::string(step == 3 ? "HTTP/1.1 503 Service Unavailable\r\n"
													 : "HTTP/1.1 410 Gone\r\n") +
							   "Content-Type: application/json\r\nContent-Length: " +
							   std::to_string(control.size()) + "\r\n\r\n" + control;
					} else {
						check(headers.starts_with("POST /v2/distribution/session/close "));
						wire = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
					}
					for (std::size_t offset = 0; offset < wire.size();) {
						const auto count = std::min<std::size_t>(17, wire.size() - offset);
						asio::write(socket, asio::buffer(wire.data() + offset, count));
						offset += count;
					}
				}
			} catch (...) {
				failure = std::current_exception();
			}
		});
	}
	~SessionServer() { worker.join(); }
};
asio::awaitable<void> session_lifecycle(t::ClientOptions options) {
	auto session = co_await t::open_session(options);
	check(session.valid());
	auto event = co_await t::next_session(options, session, {}, 0ms);
	auto &offer = std::get<t::SessionOffer>(event);
	check(offer.window == 1 && offer.update.target == cursor());
	bool rejected = false;
	try {
		(void)co_await t::next_session(options, session, {}, 0ms);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	check(rejected && session.valid());
	auto changed = options;
	changed.namespaces.push_back("changed");
	rejected = false;
	try {
		(void)co_await t::next_session(changed, session,
									   {t::PublicationFeedback{offer.window, offer.update.target}}, 0ms);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	check(rejected && session.valid());
	for (unsigned fault = 0; fault != 3; ++fault) {
		auto routed = options;
		if (fault == 0)
			routed.endpoint.port(options.endpoint.port() == 65535 ? 1 : options.endpoint.port() + 1);
		else if (fault == 1)
			routed.hostname = "changed.example";
		else
			routed.tls_context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		bool refused = false;
		try {
			(void)co_await t::next_session(routed, session,
										   t::PublicationFeedback{offer.window, offer.update.target}, 0ms);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		check(refused && session.valid());
		refused = false;
		try {
			co_await t::close_session(routed, session);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		check(refused && session.valid());
	}
	const t::PublicationFeedback feedback{offer.window, offer.update.target};
	for (;;) {
		d::WorkBudget work;
		if (offer.update.snapshot->discard(work))
			break;
		co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
	}
	event = co_await t::next_session(options, session, feedback, 0ms);
	check(std::holds_alternative<t::SessionIdle>(event) && session.valid());
	event = co_await t::next_session(options, session, {}, 0ms);
	check(std::get<t::SessionReset>(event).reason == t::SessionResetReason::SourceBehind && session.valid());
	event = co_await t::next_session(options, session, {}, 0ms);
	check(std::get<t::SessionReset>(event).reason == t::SessionResetReason::Expired && !session.valid());
	co_await t::close_session(options, session);
	co_await t::close_session(options, session);
}
} // namespace
int main(int argc, char **argv) {
	try {
		check(argc == 2);
		const std::filesystem::path cert(argv[1]);
		auto server_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(
			*server_tls,
			{(cert / "one.pem").string(), (cert / "one.key").string(), (cert / "ca.pem").string()},
			reactor::tls::Role::Server);
		auto client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*client_tls, {"", "", (cert / "ca.pem").string()},
								reactor::tls::Role::Client);
		for (bool compressed : {false, true})
			for (unsigned mode = 0; mode < 12; ++mode) {
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
					} catch (const t::SessionResetError &error) {
						constexpr std::array reasons{
							t::SessionResetReason::NotLeader,	  t::SessionResetReason::Unauthorized,
							t::SessionResetReason::AdmissionFull, t::SessionResetReason::SourceBehind,
							t::SessionResetReason::Unavailable,	  t::SessionResetReason::Forbidden};
						check(mode >= 5 && error.reason() == reasons[mode - 5]);
						rejected = true;
					} catch (const t::SourceUnavailableError &) {
						check(mode == 1 || mode == 2 || mode == 11);
						rejected = true;
					} catch (const t::SourceTrustError &) {
						check(mode == 4);
						rejected = true;
					} catch (const std::exception &) {
						check(mode > 0 && mode < 4);
						rejected = true;
					}
					check(rejected == (mode != 0));
					if (mode == 1 || mode == 2)
						check(std::chrono::steady_clock::now() - began < 500ms);
				}
				check(account->used() == 0);
			}

		for (bool compressed : {false, true}) {
			auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
			{
				SessionServer server(server_tls, compressed);
				t::ClientOptions options;
				options.endpoint = server.acceptor.local_endpoint();
				options.hostname = "localhost";
				options.token = "0123456789abcdef0123456789abcdef";
				options.tls_context = client_tls;
				options.configured = cursor();
				options.base = cursor();
				options.compression = compressed;
				options.limits.storage_account = account;
				options.timeout = 2s;
				asio::io_context io;
				auto pending = asio::co_spawn(io, session_lifecycle(options), asio::use_future);
				io.run();
				pending.get();
				if (server.failure)
					std::rethrow_exception(server.failure);
			}
			check(account->used() == 0);
		}
		std::cout << "incremental TLS client and deadline cleanup passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
