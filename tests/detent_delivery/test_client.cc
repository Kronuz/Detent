#include "detent_delivery/client.hh"
#include "detent_distribution/wire_codec.hh"
#include <asio.hpp>
#include <filesystem>
#include <iostream>
#include <source_location>
#include <thread>
namespace d = detent::distribution;
namespace t = detent::delivery;
namespace m = msgpack::bounded;
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
	m::AppendPacker(out).pack_array(7);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_array(2);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_array(5);
	const auto id = cursor();
	for (const auto *part : {&id.cluster, &id.group, &id.restore_epoch})
		m::AppendPacker(out).pack_bin({part->data(), part->size()});
	m::AppendPacker(out).pack_bin({id.scope.data(), id.scope.size()});
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_array(4);
	for (unsigned i = 0; i < 4; ++i)
		m::AppendPacker(out).pack_uint64(0);
	m::AppendPacker(out).pack_array(0);
	m::AppendPacker(out).pack_array(0);
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
				if (mode == 12)
					bytes.insert(bytes.find("\r\n") + 2, "Connection: close\r\n");
				if (mode == 13) {
					bytes += "trailing";
					asio::write(socket, asio::buffer(bytes), error);
					return;
				}
				if (mode == 14 || mode == 15) {
					const std::string body = mode == 14 ? "{\"version\":1}" : "{\"error\":false}";
					bytes =
						std::string(mode == 14 ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 503 Unavailable\r\n") +
						"Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
						"\r\n\r\n" + body;
				}
				if (mode == 16)
					bytes =
						"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 20\r\n\r\nService Unavailable\n";
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
	SessionServer(std::shared_ptr<asio::ssl::context> context, bool compressed, bool persistent = false) {
		worker = std::thread([&, context = std::move(context), compressed, persistent] {
			try {
				std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>> connection;
				for (unsigned sequence = 0; sequence < (persistent ? 14u : 7u); ++sequence) {
					const auto step = sequence % 7;
					if (!persistent || !connection) {
						connection = std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(
							acceptor.accept(), *context);
						connection->handshake(asio::ssl::stream_base::server);
					}
					auto &socket = *connection;
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
						if (step == 5) {
							const std::string control = "{\"error\":\"admission_full\"}";
							wire = "HTTP/1.1 503 Service Unavailable\r\nContent-Type: "
								   "application/json\r\nContent-Length: " +
								   std::to_string(control.size()) + "\r\n\r\n" + control;
						} else
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
	~SessionServer() {
		if (worker.joinable())
			worker.join();
	}
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
	bool close_refused = false;
	try {
		co_await t::close_session(options, session);
	} catch (const t::SessionResetError &error) {
		close_refused = error.reason() == t::SessionResetReason::AdmissionFull;
		check(std::string_view(error.what()) == "distribution session reset: admission_full");
	}
	check(close_refused);
	co_await t::close_session(options, session);
	co_await t::close_session(options, session);
}
asio::awaitable<t::ReceivedUpdate> request(t::ClientOptions options, unsigned mode) {
	if (mode == 14) {
		(void)co_await t::open_session(std::move(options));
		co_return t::ReceivedUpdate{};
	}
	co_return co_await t::receive(std::move(options));
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
		for (bool cached : {false, true}) {
			for (bool compressed : {false, true})
				for (unsigned mode = 0; mode < 17; ++mode) {
					auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
					{
						Server server(server_tls, mode == 4 ? 0 : mode, compressed);
						asio::io_context io;
						t::ClientOptions options;
						options.endpoint = server.acceptor.local_endpoint();
						options.hostname = mode == 4 ? "wrong.example" : "localhost";
						options.token = "0123456789abcdef0123456789abcdef";
						options.tls_context = client_tls;
						options.configured = cursor();
						options.compression = compressed;
						options.limits.storage_account = account;
						options.timeout = mode == 1 || mode == 2 ? 20ms : 2s;
						auto cache =
							cached ? t::make_connection_cache(io.get_executor(), 1, account) : nullptr;
						options.connections = cache;
						const auto began = std::chrono::steady_clock::now();
						auto pending =
							asio::co_spawn(io, request(std::move(options), mode), asio::use_future);
						io.run();
						bool rejected = false, accepted = false;
						try {
							auto received = pending.get();
							accepted = true;
							check((mode == 0 || mode == 12) && received.snapshot &&
								  received.snapshot->records().size() == 0 && received.target == cursor());
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
						} catch (const std::exception &error) {
							check((mode > 0 && mode < 4) || mode >= 13);
							if (mode == 16)
								check(std::string_view(error.what()) ==
									  "missing delivery response header: content-type (HTTP 503)");
							rejected = true;
						}
						check(rejected == (mode != 0 && mode != 12));
						check(accepted == (mode == 0 || mode == 12));
						if (cache) {
							const auto stats = t::connection_cache_stats(cache);
							check(stats.leased == 0);
							if ((mode >= 1 && mode <= 4) || mode >= 11)
								check(stats.live == 0);
						}
						if (mode == 1 || mode == 2)
							check(std::chrono::steady_clock::now() - began < 500ms);
					}
					check(account->used() == 0);
				}
		}
		for (bool persistent : {false, true}) {
			for (bool compressed : {false, true}) {
				auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
				{
					SessionServer server(server_tls, compressed, persistent);
					asio::io_context io;
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
					if (persistent)
						options.connections = t::make_connection_cache(io.get_executor(), 1, account);
					auto pending = asio::co_spawn(io, session_lifecycle(options), asio::use_future);
					io.run();
					pending.get();
					if (persistent) {
						io.restart();
						auto again = asio::co_spawn(io, session_lifecycle(options), asio::use_future);
						io.run();
						again.get();
						const auto stats = t::connection_cache_stats(options.connections);
						check(stats.connects == 1 && stats.handshakes == 1 && stats.reuses == 13 &&
							  stats.leased == 0);
						options.connections.reset();
					}
					server.worker.join();
					if (server.failure)
						std::rethrow_exception(server.failure);
				}
				check(account->used() == 0);
			}
		}
		std::cout << "incremental TLS client and deadline cleanup passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
