#include "../../src/detent_delivery/client_probe_internal.hh"
#include "probe_fixture.hh"
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <atomic>
#include <exception>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>

namespace d = detent::distribution;
namespace t = detent::delivery;
namespace p = t::detail;
using namespace std::chrono_literals;
using namespace probe_fixture;
namespace {
enum class Case { Lifecycle, Reference, Semantic, Corrupt, Truncated, Cancelled, Deadline, InvalidReference };
struct Server {
	asio::io_context io;
	asio::ip::tcp::acceptor acceptor{io, {asio::ip::address_v4::loopback(), 0}};
	const asio::ip::tcp::endpoint endpoint = acceptor.local_endpoint();
	std::atomic<bool> held{false}, original_reaped{false};
	std::exception_ptr failure;
	unsigned requests = 0;
	std::jthread worker;
	Server(std::shared_ptr<asio::ssl::context> context, Case scenario, bool compressed) {
		acceptor.non_blocking(true);
		worker = std::jthread([&, context = std::move(context), scenario, compressed](std::stop_token stop) {
			try {
				const auto count = scenario == Case::Lifecycle ? 7u : 3u;
				for (unsigned step = 0; step != count; ++step) {
					asio::ip::tcp::socket accepted(io);
					const auto deadline = std::chrono::steady_clock::now() + 10s;
					for (;;) {
						asio::error_code error;
						acceptor.accept(accepted, error);
						if (!error)
							break;
						if (stop.stop_requested())
							return;
						check(error == asio::error::would_block || error == asio::error::try_again);
						check(std::chrono::steady_clock::now() < deadline);
						std::this_thread::sleep_for(1ms);
					}
					asio::ssl::stream<asio::ip::tcp::socket> socket(std::move(accepted), *context);
					socket.handshake(asio::ssl::stream_base::server);
					asio::streambuf input(65536);
					asio::read_until(socket, input, "\r\n\r\n");
					auto text = [&] {
						return std::string(asio::buffers_begin(input.data()), asio::buffers_end(input.data()));
					};
					const auto header = text();
					const auto begin = header.find("\r\n\r\n") + 4;
					const auto length = std::stoul(header.substr(header.find("Content-Length: ") + 16));
					if (input.size() < begin + length)
						asio::read(socket, input, asio::transfer_exactly(begin + length - input.size()));
					const auto body = text().substr(begin);
					++requests;
					std::string bytes;
					if (step == 0) {
						check(header.starts_with("POST /v2/distribution/session/open "));
						bytes = control(200, "{\"version\":1,\"session\":\"" + session_hex() +
							"\",\"window\":0,\"mode\":\"snapshot\",\"wait_ms\":1000}");
					} else if (step + 1 == count) {
						check(header.starts_with("POST /v2/distribution/session/close "));
						bytes = control(204);
					} else {
						check(header.starts_with("POST /v2/distribution/session/next "));
						if (step > 1)
							check(body.find("\"after\":1") != std::string::npos &&
								  body.find("\"published\":{") != std::string::npos);
						if (step == 1) {
							const auto payload = scenario == Case::Lifecycle ? std::string(200000, 'x')
								: scenario == Case::InvalidReference ? std::string("not a profile-1 message")
								: semantic();
							bytes = response(payload, compressed).bytes;
							if (scenario == Case::Corrupt)
								bytes.back() ^= 1;
							if (scenario == Case::Truncated || scenario == Case::Cancelled || scenario == Case::Deadline)
								bytes.resize(bytes.find("\r\n\r\n") + 4 + 48);
						} else if (step == 2)
							bytes = control(503, "{\"error\":\"admission_full\"}");
						else if (step == 3)
							bytes = control(204);
						else if (step == 4)
							bytes = control(503, "{\"error\":\"source_behind\"}");
						else
							bytes = control(503, "{\"error\":\"source_not_leader\"}");
					}
					asio::write(socket, asio::buffer(bytes));
					if (step == 1 && (scenario == Case::Cancelled || scenario == Case::Deadline)) {
						held.store(true, std::memory_order_release);
						std::array<char, 1> buffer;
						asio::error_code error;
						(void)socket.read_some(asio::buffer(buffer), error);
						check(bool(error));
						original_reaped.store(true, std::memory_order_release);
					}
				}
			} catch (...) {
				failure = std::current_exception();
			}
		});
	}
};
template <typename Function> asio::awaitable<void> refuses(Function function) {
	bool rejected = false;
	try {
		co_await function();
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	check(rejected);
}
asio::awaitable<void> exercise(t::ClientOptions options, Server &server, Case scenario) {
	auto handle = co_await t::open_session(options, true);
	const auto baseline = options.limits.storage_account->used();
	std::exception_ptr failure;
	try {
		if (scenario == Case::Lifecycle) {
			auto changed = options;
			changed.endpoint.port(changed.endpoint.port() == 65535 ? 1 : changed.endpoint.port() + 1);
			co_await refuses([&]() -> asio::awaitable<void> { (void)co_await p::ProbeAccess::next(changed, handle, {}, 0ms); });
			const auto event = co_await p::ProbeAccess::next(options, handle, {}, 0ms);
			const auto &offer = std::get<p::ProbeOffer>(event);
			check(offer.target == cursor() && offer.window == 1 && offer.logical_bytes == 200000 &&
				  offer.digest == digest(std::string(200000, 'x')) &&
				  options.limits.storage_account->used() == baseline);
			const t::PublicationFeedback feedback{offer.window, offer.target};
			co_await refuses([&]() -> asio::awaitable<void> { (void)co_await t::next_session(options, handle, feedback, 0ms); });
			co_await refuses([&]() -> asio::awaitable<void> { (void)co_await p::ProbeAccess::reference(options, handle, feedback, 0ms); });
			co_await refuses([&]() -> asio::awaitable<void> { (void)co_await p::ProbeAccess::next(options, handle, t::PublicationFeedback{2, cursor()}, 0ms); });
			auto reset = co_await p::ProbeAccess::next(options, handle, feedback, 0ms);
			check(std::get<t::SessionReset>(reset).reason == t::SessionResetReason::AdmissionFull && handle.valid());
			check(std::holds_alternative<t::SessionIdle>(co_await p::ProbeAccess::next(options, handle, feedback, 0ms)));
			reset = co_await p::ProbeAccess::next(options, handle, feedback, 0ms);
			check(std::get<t::SessionReset>(reset).reason == t::SessionResetReason::SourceBehind && handle.valid());
			reset = co_await p::ProbeAccess::next(options, handle, feedback, 0ms);
			check(std::get<t::SessionReset>(reset).reason == t::SessionResetReason::NotLeader && !handle.valid());
		} else if (scenario == Case::Reference) {
			const auto event = co_await p::ProbeAccess::reference(options, handle, {}, 0ms);
			check(std::get<p::ProbeOffer>(event).digest == digest(semantic()) &&
				  options.limits.storage_account->used() == baseline);
			co_await refuses([&]() -> asio::awaitable<void> { (void)co_await p::ProbeAccess::next(options, handle, t::PublicationFeedback{1, cursor()}, 0ms); });
		} else if (scenario == Case::Semantic) {
			auto event = co_await t::next_session(options, handle, {}, 0ms);
			auto &offer = std::get<t::SessionOffer>(event);
			co_await refuses([&]() -> asio::awaitable<void> { (void)co_await p::ProbeAccess::next(options, handle, t::PublicationFeedback{offer.window, offer.update.target}, 0ms); });
			while (true) {
				d::WorkBudget budget;
				if (offer.update.snapshot->discard(budget))
					break;
			}
		} else if (scenario == Case::Cancelled) {
			asio::cancellation_signal cancellation;
			bool rejected = false;
			auto attempt = [&]() -> asio::awaitable<void> {
				try {
					(void)co_await asio::co_spawn(co_await asio::this_coro::executor,
						p::ProbeAccess::next(options, handle, {}, 0ms),
						asio::bind_cancellation_slot(cancellation.slot(), asio::use_awaitable));
				} catch (const std::exception &) {
					rejected = true;
				}
			};
			auto cancel = [&]() -> asio::awaitable<void> {
				asio::steady_timer timer(co_await asio::this_coro::executor);
				const auto deadline = std::chrono::steady_clock::now() + 5s;
				while (!server.held.load(std::memory_order_acquire)) {
					check(std::chrono::steady_clock::now() < deadline);
					timer.expires_after(1ms);
					co_await timer.async_wait(asio::use_awaitable);
				}
				co_await refuses([&]() -> asio::awaitable<void> { (void)co_await p::ProbeAccess::next(options, handle, {}, 0ms); });
				co_await refuses([&]() -> asio::awaitable<void> { co_await t::close_session(options, handle); });
				cancellation.emit(asio::cancellation_type::terminal);
			};
			using namespace asio::experimental::awaitable_operators;
			co_await (attempt() && cancel());
			check(rejected && !handle.valid());
		} else {
			if (scenario == Case::Deadline)
				options.timeout = 500ms;
			bool rejected = false;
			try {
				if (scenario == Case::InvalidReference)
					(void)co_await p::ProbeAccess::reference(options, handle, {}, 0ms);
				else
					(void)co_await p::ProbeAccess::next(options, handle, {}, 0ms);
			} catch (const t::SourceUnavailableError &) {
				check(scenario == Case::Truncated || scenario == Case::Deadline);
				rejected = true;
			} catch (const std::invalid_argument &) {
				check(scenario == Case::Corrupt || scenario == Case::InvalidReference);
				rejected = true;
			}
			check(rejected && !handle.valid());
		}
	} catch (...) {
		failure = std::current_exception();
	}
	try {
		options.timeout = 3s;
		co_await t::close_session(options, handle);
	} catch (...) {
		if (!failure)
			failure = std::current_exception();
	}
	if (failure)
		std::rethrow_exception(failure);
	check(!handle.valid());
}
} // namespace
int main(int argc, char **argv) {
	try {
		check(argc == 2);
		const std::filesystem::path certificates(argv[1]);
		auto server_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*server_tls, {(certificates / "one.pem").string(),
			(certificates / "one.key").string(), (certificates / "ca.pem").string()}, reactor::tls::Role::Server);
		auto client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*client_tls, {"", "", (certificates / "ca.pem").string()}, reactor::tls::Role::Client);
		for (const bool compressed : {false, true})
			for (const auto scenario : {Case::Lifecycle, Case::Reference, Case::Semantic, Case::Corrupt,
				Case::Truncated, Case::Cancelled, Case::Deadline, Case::InvalidReference}) {
				auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
				Server server(server_tls, scenario, compressed);
				t::ClientOptions options;
				options.endpoint = server.endpoint;
				options.hostname = "localhost";
				options.token = "0123456789abcdef0123456789abcdef";
				options.tls_context = client_tls;
				options.configured = cursor();
				options.namespaces = {"ns"};
				options.compression = compressed;
				options.timeout = 3s;
				options.limits.storage_account = account;
				asio::io_context io(1);
				auto done = asio::co_spawn(io, exercise(options, server, scenario), asio::use_future);
				io.run();
				std::exception_ptr failure;
				try { done.get(); } catch (...) { failure = std::current_exception(); }
				server.worker.request_stop();
				server.worker.join();
				if (server.failure)
					std::rethrow_exception(server.failure);
				if (failure)
					std::rethrow_exception(failure);
				check(server.requests == (scenario == Case::Lifecycle ? 7u : 3u) && account->used() == 0);
				if (scenario == Case::Cancelled || scenario == Case::Deadline)
					check(server.original_reaped.load(std::memory_order_acquire));
			}
		std::cout << "TLS probes, semantic references, mode binding and original drainage passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
