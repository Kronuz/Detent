#include "detent/scope.hh"
#include "detent_distribution/framing.hh"
#include "service.hh"
#ifdef DETENT_TEST_MANAGED_DELIVERY
#include "detent_delivery/client.hh"
#endif
#include "transport/tls.hh"
#include <filesystem>
#include <http_asio_tls.h>
#include <iostream>
#include <source_location>
#include <unistd.h>
namespace a = detent::authority;
namespace d = detent::distribution;
using namespace std::chrono_literals;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("distribution service invariant at " + std::to_string(where.line()));
}
std::string hex(std::span<const char> bytes) {
	std::string out;
	for (unsigned char byte : bytes) {
		out += "0123456789abcdef"[byte >> 4];
		out += "0123456789abcdef"[byte & 15];
	}
	return out;
}
struct Response {
	unsigned status;
	std::map<std::string, std::string> headers;
	std::string body;
};
Response request(asio::ip::tcp::endpoint endpoint, const std::filesystem::path &cert, std::string_view path,
				 const std::string &body, std::string_view token) {
	asio::io_context io;
	asio::ssl::context tls(asio::ssl::context::tls);
	detent::transport::configure_tls(tls, {"", "", (cert / "ca.pem").string()},
									 detent::transport::TlsRole::PublisherClient);
	asio::ssl::stream<asio::ip::tcp::socket> socket(io, tls);
	detent::transport::configure_publisher_client(socket.native_handle(), "localhost");
	socket.next_layer().connect(endpoint);
	socket.handshake(asio::ssl::stream_base::client);
	const auto wire = "POST " + std::string(path) + " HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer " +
					  std::string(token) +
					  "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
					  std::to_string(body.size()) + "\r\n\r\n" + body;
	asio::write(socket, asio::buffer(wire));
	std::string received;
	std::array<char, 4096> scratch;
	asio::error_code error;
	while (!error) {
		auto n = socket.read_some(asio::buffer(scratch), error);
		received.append(scratch.data(), n);
	}
	const auto split = received.find("\r\n\r\n");
	check(split != std::string::npos);
	Response response{static_cast<unsigned>(std::stoul(received.substr(9, 3))), {}, {}};
	auto at = received.find("\r\n") + 2;
	while (at < split) {
		const auto end = received.find("\r\n", at), colon = received.find(':', at);
		check(colon < end &&
			  response.headers
				  .emplace(received.substr(at, colon - at), received.substr(colon + 2, end - colon - 2))
				  .second);
		at = end + 2;
	}
	if (response.headers.contains("Transfer-Encoding")) {
		check(response.headers.at("Transfer-Encoding") == "chunked");
		at = split + 4;
		for (;;) {
			const auto end = received.find("\r\n", at);
			check(end != std::string::npos);
			const auto n = std::stoull(received.substr(at, end - at), nullptr, 16);
			at = end + 2;
			check(n <= received.size() - at && received.substr(at + n, 2) == "\r\n");
			response.body.append(received, at, n);
			at += n + 2;
			if (!n) {
				check(at == received.size());
				break;
			}
		}
	} else
		response.body = received.substr(split + 4);
	return response;
}
std::string decoded(const Response &response, d::UpdateKind kind, bool compressed) {
	if (response.status != 200)
		std::cerr << "distribution status " << response.status << ": " << response.body << '\n';
	check(response.status == 200 &&
		  response.headers.at("Content-Type") == "application/vnd.detent.distribution");
	check(response.headers.at("X-Detent-Profile") == "1" &&
		  response.headers.at("X-Detent-Kind") == std::to_string(static_cast<unsigned>(kind)));
	check(response.headers.at("X-Detent-Coding") == (compressed ? "zstd" : "raw"));
	d::FragmentDecoder decoder(kind, 256 * 1024 * 1024, compressed);
	auto input = std::span<const char>(response.body.data(), response.body.size());
	std::string bytes;
	while (!input.empty()) {
		d::WorkBudget work{17, 2};
		decoder.feed(input, work);
		if (auto frame = decoder.take())
			bytes.append(frame->bytes());
	}
	decoder.finish();
	return bytes;
}
} // namespace
int main(int argc, char **argv) {
	try {
		check(argc == 2);
		std::filesystem::path cert(argv[1]);
		const auto directory = std::filesystem::current_path() / ".scratch" /
							   ("distribution-service-" + std::to_string(getpid()));
		std::filesystem::create_directories(directory.parent_path());
		a::ServiceOptions options;
		options.directory = directory;
		options.requests = 8;
		options.node.context.cluster[0] = 'c';
		options.node.context.group[0] = 'g';
		options.node.context.epoch[0] = 'e';
		options.node.configuration.cluster = options.node.context.cluster;
		options.node.configuration.configuration = options.node.context.group;
		options.node.configuration.local = 1;
		options.node.configuration.voters = {1};
		const std::string token = "0123456789abcdef0123456789abcdef";
		options.credentials.push_back({token, "writer", {"ns", "secret"}});
		const auto context = options.node.context;
		const auto identity = "\"cluster\":\"" + hex(context.cluster) + "\",\"group\":\"" +
							  hex(context.group) + "\",\"epoch\":\"" + hex(context.epoch) + "\",";
		const auto scope = d::canonical_scope_identity("writer", 1, std::array<std::string_view, 1>{"ns"});
		d::Cursor cursor{context.cluster, context.group, context.epoch, scope, 1};
		const auto common =
			identity + "\"scope\":\"" + hex(scope) + "\",\"namespaces\":[\"ns\"],\"profile\":1,";
		auto base = [&](unsigned revision) {
			return "\"base\":{" + identity + "\"scope\":\"" + hex(scope) +
				   "\",\"revision\":" + std::to_string(revision) + "},";
		};
		for (unsigned run = 0; run < 2; ++run) {
			a::Service authority(options);
			auto tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
			detent::transport::configure_tls(
				*tls, {(cert / "one.pem").string(), (cert / "one.key").string(), (cert / "ca.pem").string()},
				detent::transport::TlsRole::PublisherServer);
			asio::io_context io;
			asio::ip::tcp::acceptor reservation(io, {asio::ip::address_v4::loopback(), 0});
			const auto endpoint = reservation.local_endpoint();
			reservation.close();
			http::HttpAsioService server(authority, 1, 2, 32, http::tls_connection_factory_timed(tls, 2s), 4);
			http::AsioBindOptions bind;
			bind.address = "127.0.0.1";
			server.set_bind_options(bind);
			server.start(endpoint.port());
			auto call = [&](std::string_view path, const std::string &fields) {
				return request(endpoint, cert, path, "{" + identity + fields + "}", token);
			};
			if (!run) {
				Response open;
				for (unsigned turn = 0; turn < 50; ++turn) {
					open = call("/v2/open",
								"\"expected_generation\":0,\"nonce\":\"6e000000000000000000000000000000\"");
					if (open.status == 200)
						break;
					std::this_thread::sleep_for(20ms);
				}
				check(open.status == 200);
				check(call("/v2/transactions",
						   "\"generation\":1,\"sequence\":1,\"namespace\":\"ns\",\"operations\":[{\"key\":"
						   "\"ns:a\",\"value\":{\"type\":\"int\",\"default\":7}}]")
						  .status == 200);
			} else {
				// Storage recovery precedes election. Wait for a quorum boundary
				// rather than assuming construction makes this node a leader.
				bool elected = false;
				for (unsigned turn = 0; turn < 50; ++turn) {
					if (call("/v2/read", "\"namespace\":\"ns\",\"keys\":[]").status == 200) {
						elected = true;
						break;
					}
					std::this_thread::sleep_for(20ms);
				}
				check(elected);
			}
			for (bool compressed : {false, true}) {
				const auto response =
					request(endpoint, cert, "/v2/distribution",
							"{" + common + "\"coding\":\"" + (compressed ? "zstd" : "raw") + "\"}", token);
				auto bytes = decoded(response, d::UpdateKind::Snapshot, compressed);
				cursor.revision = std::stoull(response.headers.at("X-Detent-Revision"));
				d::SnapshotDecoder decoder(cursor, {"ns"});
				auto input = std::span<const char>(bytes.data(), bytes.size());
				while (!input.empty()) {
					d::WorkBudget work;
					decoder.feed(input, work);
				}
				auto snapshot = decoder.finish();
				check(snapshot.records().size() == 1);
			}
			auto response = request(endpoint, cert, "/v2/distribution",
									"{" + common + base(0) + "\"coding\":\"raw\"}", token);
			decoded(response, run ? d::UpdateKind::Snapshot : d::UpdateKind::Delta, false);
			if (!run) {
				check(call("/v2/transactions",
						   "\"generation\":1,\"sequence\":2,\"namespace\":\"secret\",\"operations\":[{"
						   "\"key\":\"secret:a\",\"value\":{\"type\":\"int\",\"default\":99}}]")
						  .status == 200);
				response = request(endpoint, cert, "/v2/distribution",
								   "{" + common + base(1) + "\"coding\":\"raw\"}", token);
				auto bytes = decoded(response, d::UpdateKind::Progress, false);
				d::DeltaDecoder decoder(cursor, {"ns"}, {}, d::UpdateKind::Progress);
				auto input = std::span<const char>(bytes.data(), bytes.size());
				while (!input.empty()) {
					d::WorkBudget work;
					decoder.feed(input, work);
				}
				check(decoder.finish().target().revision == 2);
			}
			response = request(endpoint, cert, "/v2/distribution",
							   "{" + common + base(2) + "\"coding\":\"raw\"}", token);
			decoded(response, d::UpdateKind::Snapshot, false);
			check(request(endpoint, cert, "/v2/distribution",
						  "{" + common + "\"coding\":\"raw\",\"profile\":1}", token)
					  .status == 400);
#ifdef DETENT_TEST_MANAGED_DELIVERY
			auto client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
			detent::transport::configure_tls(*client_tls, {"", "", (cert / "ca.pem").string()},
											 detent::transport::TlsRole::PublisherClient);
			for (bool compressed : {false, true}) {
				auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
				{
					detent::delivery::ClientOptions receive_options;
					receive_options.endpoint = endpoint;
					receive_options.hostname = "localhost";
					receive_options.token = token;
					receive_options.tls_context = client_tls;
					receive_options.configured = cursor;
					receive_options.namespaces = {"ns"};
					receive_options.compression = compressed;
					receive_options.limits.storage_account = account;
					asio::io_context client_io;
					auto pending = asio::co_spawn(
						client_io, detent::delivery::receive(std::move(receive_options)), asio::use_future);
					client_io.run();
					auto received = pending.get();
					check(received.snapshot && received.target.revision == 2 &&
						  received.snapshot->records().size() == 1 &&
						  received.snapshot->records()[0].key == "ns:a");
					for (;;) {
						d::WorkBudget work;
						if (received.snapshot->discard(work))
							break;
					}
				}
				check(account->used() == 0);
			}
#endif
			server.stop();
		}
		std::filesystem::remove_all(directory);
		std::cout << "authenticated TLS distribution passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
