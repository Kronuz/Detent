#pragma once
#include "relay_service_fixture.hh"
#include <http_asio.h>
#include <test/support/held_write_stream.h>

namespace held_session_fixture {
namespace r = detent::relay;
enum class Outcome { Written, AbandonNext, ShutdownNext, ShutdownOpen };
class Spy final : public http::HttpHandler {
  public:
	r::Service service;
	unsigned intake = 0, handled = 0;
	explicit Spy(std::shared_ptr<r::ServiceRuntime> runtime) : service(std::move(runtime)) {}
	std::unique_ptr<http::BodySink> on_request_body(http::Request &request) override {
		++intake;
		return service.on_request_body(request);
	}
	void handle(const http::Request &request, http::ResponseWriter &writer) override {
		++handled;
		service.handle(request, writer);
	}
	void on_error(std::exception_ptr error, const http::Request &request,
				  http::ResponseWriter &writer) override {
		service.on_error(error, request, writer);
	}
};
asio::awaitable<void> serve(asio::ip::tcp::acceptor &acceptor, std::shared_ptr<asio::ssl::context> tls,
							std::shared_ptr<http::test::WriteGate> gate, Spy &handler,
							reactor::Reactor &reactor, http::AsioResponseOptions &options) {
	auto socket = co_await acceptor.async_accept(asio::use_awaitable);
	acceptor.close();
	asio::ssl::stream<asio::ip::tcp::socket> stream(std::move(socket), *tls);
	co_await stream.async_handshake(asio::ssl::stream_base::server, asio::use_awaitable);
	co_await http::detail::serve_connection(http::test::HeldWriteStream(std::move(stream), gate), &handler,
											&reactor, &options);
}
template <class Predicate>
asio::awaitable<void> until(Predicate ready, r::ServiceRuntime &runtime,
							std::chrono::steady_clock::time_point deadline) {
	const auto executor = co_await asio::this_coro::executor;
	while (!ready()) {
		if (std::chrono::steady_clock::now() >= deadline)
			throw std::runtime_error("held session fixture deadline");
		runtime.step(r::Sessions::Clock::now());
		co_await asio::post(executor, asio::use_awaitable);
	}
}
asio::awaitable<void> qualify(reactor::Reactor &reactor, std::shared_ptr<r::Bundle> bundle,
							  d::RetirementHandle<d::EncodedUpdate> expected,
							  std::shared_ptr<d::StorageLedger> account,
							  std::shared_ptr<asio::ssl::context> server_tls,
							  std::shared_ptr<asio::ssl::context> client_tls, std::uint32_t records,
							  Outcome outcome = Outcome::Written) {
	const auto executor = co_await asio::this_coro::executor;
	const auto baseline = account->used();
	std::exception_ptr failure;
	{
		const auto target = expected->target();
		const std::array<std::string_view, 1> name{"app"};
		const auto names =
			records ? std::span<const std::string_view>(name) : std::span<const std::string_view>{};
		r::ConfiguredCredential credential;
		credential.authorization.principal.identity.fill('p');
		credential.authorization.principal.policy_epoch = 1;
		credential.authorization.grants.fill('g');
		credential.authorization.bundles[0] = 1;
		const std::string token = "0123456789abcdef0123456789abcdef";
		class SHA256 token_hash;
		token_hash.add(token.data(), token.size());
		token_hash.getHash(credential.token_hash.data());
		r::ServiceAccounts accounts{std::make_shared<d::StorageLedger>(account, 4 * 1024 * 1024),
									{std::make_shared<d::StorageLedger>(account, 4 * 1024 * 1024),
									 std::make_shared<d::StorageLedger>(account, 8 * 1024 * 1024),
									 std::make_shared<d::StorageLedger>(account, 2 * 1024 * 1024)}};
		auto registry = std::allocate_shared<r::Registry>(
			d::StorageAllocator<r::Registry>(accounts.metadata), accounts.metadata,
			std::array{r::RegistryBundle{target, credential.authorization.principal, names}},
			std::array{credential});
		const std::array bindings{r::SessionBinding{bundle, credential.authorization.principal, target}};
		const r::ServiceLimits service_limits;
		auto runtime =
			std::allocate_shared<r::ServiceRuntime>(d::StorageAllocator<r::ServiceRuntime>(accounts.metadata),
													accounts, registry, bindings, service_limits);
		std::array<std::size_t, 3> lane_baseline{};
		for (std::size_t lane = 0; lane < lane_baseline.size(); ++lane)
			lane_baseline[lane] = accounts.requests[lane]->used();
		Spy handler(runtime);
		asio::ip::tcp::acceptor acceptor(executor, {asio::ip::address_v4::loopback(), 0});
		auto gate = std::make_shared<http::test::WriteGate>(executor);
		gate->arm();
		http::AsioResponseOptions response_options;
		response_options.request_timeout = 20s;
		asio::cancellation_signal server_cancel, client_cancel;
		bool serving = true, pending = false;
		asio::co_spawn(
			executor, serve(acceptor, server_tls, gate, handler, reactor, response_options),
			asio::bind_cancellation_slot(server_cancel.slot(), [&](std::exception_ptr) { serving = false; }));
		t::ClientOptions options;
		options.endpoint = acceptor.local_endpoint();
		options.hostname = "localhost";
		options.token = token;
		options.tls_context = client_tls;
		options.configured = target;
		if (records)
			options.namespaces = {"app"};
		options.limits.storage_account = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
		const auto client_baseline = options.limits.storage_account->used();
		options.timeout = 10s;
		options.connections = t::make_connection_cache(executor, 1, options.limits.storage_account);
		t::SessionHandle session;
		std::optional<t::SessionEvent> event;
		std::exception_ptr operation_error;
		const auto deadline = std::chrono::steady_clock::now() + 20s;
		const auto received = [&] {
			if (!pending && operation_error)
				std::rethrow_exception(operation_error);
			return !pending;
		};
		const auto next = [&](std::optional<t::PublicationFeedback> feedback) {
			pending = true;
			operation_error = {};
			event.reset();
			asio::co_spawn(executor, t::next_session(options, session, feedback, 0ms),
						   asio::bind_cancellation_slot(
							   client_cancel.slot(), [&](std::exception_ptr error, t::SessionEvent result) {
								   operation_error = error;
								   if (!error)
									   event.emplace(std::move(result));
								   pending = false;
							   }));
		};
		const auto terminal = [&](bool open) -> asio::awaitable<void> {
			const bool abandon = outcome == Outcome::AbandonNext;
			const auto lane = open ? 0u : 1u;
			const auto handlers = open ? 1u : 2u;
			if (abandon)
				runtime->close();
			else
				reactor.abort_all();
			const auto owner_round =
				std::max({service_limits.opens, service_limits.polls, service_limits.closes});
			for (std::size_t turn = 0; turn < owner_round; ++turn) {
				runtime->step(r::Sessions::Clock::now());
				co_await asio::post(executor, asio::use_awaitable);
			}
			const auto retained = runtime->status();
			check(retained.closing == abandon && retained.sessions == 1);
			check(retained.requests ==
				  (open ? std::array<std::size_t, 3>{1, 0, 0} : std::array<std::size_t, 3>{0, 1, 0}));
			check(retained.bootstrap == (open ? 0u : 1u) && retained.catchup == 0);
			check(!runtime->drained() && accounts.requests[lane]->used() > lane_baseline[lane]);
			check(handler.intake == handlers && handler.handled == handlers && gate->held());
			check(gate->release() && !gate->release());
			co_await until(
				[&] {
					const auto status = runtime->status();
					return !pending && !serving && status.requests == std::array<std::size_t, 3>{} &&
						   status.sessions == 0 && status.bootstrap == 0 && status.catchup == 0;
				},
				*runtime, deadline);
			check(bool(operation_error));
			check(!event);
			check(!session.valid());
			check(runtime->status().closing == abandon && gate->observed() == gate->delivered());
			for (unsigned turn = 0; turn < 16; ++turn) {
				runtime->step(r::Sessions::Clock::now());
				co_await asio::post(executor, asio::use_awaitable);
			}
			check(handler.intake == handlers && handler.handled == handlers);
			check(runtime->status().requests == std::array<std::size_t, 3>{} &&
				  runtime->status().bootstrap == 0 && runtime->status().catchup == 0 &&
				  runtime->status().sessions == 0);
			if (abandon) {
				std::size_t retained_controls = 0;
				for (std::size_t index = 0; index < lane_baseline.size(); ++index) {
					check(accounts.requests[index]->used() >= lane_baseline[index]);
					retained_controls += accounts.requests[index]->used() - lane_baseline[index];
				}
				std::cout << "registry-held weak controls retain " << retained_controls
						  << " admitted bytes until Reactor abort\n";
				reactor.abort_all();
			}
			co_await until(
				[&] {
					for (std::size_t index = 0; index < lane_baseline.size(); ++index)
						if (accounts.requests[index]->used() != lane_baseline[index])
							return false;
					return true;
				},
				*runtime, deadline);
		};
		try {
			pending = true;
			asio::co_spawn(executor, t::open_session(options, true),
						   asio::bind_cancellation_slot(
							   client_cancel.slot(), [&](std::exception_ptr error, t::SessionHandle result) {
								   operation_error = error;
								   if (!error)
									   session = std::move(result);
								   pending = false;
							   }));
			co_await until([&] { return received() && gate->held(); }, *runtime, deadline);
			check(session.valid() && session.identity() != d::Identity{} && session.maximum_wait() == 30s);
			check(gate->held_head().status == 200 && gate->held_head().content_length &&
				  !gate->held_head().chunked);
			check(runtime->status().requests == std::array<std::size_t, 3>{1, 0, 0});
			check(runtime->status().sessions == 1 && handler.intake == 1 && handler.handled == 1);
			check(gate->observed() == gate->delivered() + 1 &&
				  accounts.requests[0]->used() > lane_baseline[0]);
			next({});
			co_await http::client::before(http::client::Deadline(10s), gate->wait_read());
			check(pending && handler.intake == 1 && handler.handled == 1);
			check(runtime->status().requests == std::array<std::size_t, 3>{1, 0, 0});
			if (outcome == Outcome::ShutdownOpen) {
				co_await terminal(true);
			} else {
				check(gate->release() && !gate->release());
				gate->arm();
				co_await until([&] { return received() && gate->held(); }, *runtime, deadline);
				auto &offer = std::get<t::SessionOffer>(*event);
				check(offer.window == 1 && offer.update.target == target && offer.update.snapshot);
				check(gate->held_head().status == 200 && gate->held_head().chunked &&
					  !gate->held_head().content_length);
				check(gate->held_head().get("x-detent-window") == "1" &&
					  gate->held_head().get("x-detent-session") ==
						  relay_service_fixture::hex(session.identity()));
				check(offer.update.snapshot->records().size() == records);
				if (records) {
					const auto last = std::to_string(records - 1);
					check(offer.update.snapshot->records()[0].key == "app:key000000");
					check(offer.update.snapshot->records()[records - 1].key ==
						  "app:key" + std::string(6 - last.size(), '0') + last);
				}
				const t::PublicationFeedback feedback{offer.window, offer.update.target};
				for (;;) {
					d::WorkBudget budget;
					if (offer.update.snapshot->discard(budget))
						break;
					co_await asio::post(executor, asio::use_awaitable);
				}
				co_await until([&] { return runtime->status().requests[0] == 0; }, *runtime, deadline);
				check(runtime->status().requests == std::array<std::size_t, 3>{0, 1, 0});
				check(runtime->status().bootstrap == 1 && handler.intake == 2 && handler.handled == 2);
				next(feedback);
				co_await http::client::before(http::client::Deadline(10s), gate->wait_read());
				check(pending && handler.intake == 2 && handler.handled == 2);
				check(runtime->status().requests == std::array<std::size_t, 3>{0, 1, 0});
				check(runtime->status().bootstrap == 1 && accounts.requests[1]->used() > lane_baseline[1]);
				if (outcome != Outcome::Written) {
					co_await terminal(false);
				} else {
					check(gate->release() && !gate->release());
					co_await until(received, *runtime, deadline);
					check(std::holds_alternative<t::SessionIdle>(*event) && session.valid());
					co_await until(
						[&] {
							const auto status = runtime->status();
							return status.requests == std::array<std::size_t, 3>{} && status.bootstrap == 0 &&
								   status.catchup == 0 && status.sessions == 1;
						},
						*runtime, deadline);
					next(feedback);
					co_await until(received, *runtime, deadline);
					check(std::holds_alternative<t::SessionIdle>(*event));
					co_await until([&] { return runtime->status().requests == std::array<std::size_t, 3>{}; },
								   *runtime, deadline);
					pending = true;
					asio::co_spawn(
						executor, t::close_session(options, session),
						asio::bind_cancellation_slot(client_cancel.slot(), [&](std::exception_ptr error) {
							operation_error = error;
							pending = false;
						}));
					co_await until([&] { return received() && runtime->status().sessions == 0; }, *runtime,
								   deadline);
					check(!session.valid());
					const auto stats = t::connection_cache_stats(options.connections);
					check(stats.connects == 1 && stats.handshakes == 1 && stats.reuses == 4);
				}
			}
		} catch (...) {
			failure = std::current_exception();
		}
		co_await asio::this_coro::throw_if_cancelled(false);
		runtime->close();
		client_cancel.emit(asio::cancellation_type::all);
		server_cancel.emit(asio::cancellation_type::all);
		reactor.abort_all();
		gate->release();
		asio::error_code ignored;
		acceptor.close(ignored);
		while (pending) {
			runtime->step(r::Sessions::Clock::now());
			co_await asio::post(executor, asio::use_awaitable);
		}
		event.reset();
		session = {};
		options.connections.reset();
		while (serving || !runtime->drained()) {
			runtime->step(r::Sessions::Clock::now());
			co_await asio::post(executor, asio::use_awaitable);
		}
		check(options.limits.storage_account->used() == client_baseline);
	}
	check(account->used() == baseline);
	if (failure)
		std::rethrow_exception(failure);
	std::cout << "held TLS Open/Next originals preserve window feedback and "
				 "request admission\n";
}
} // namespace held_session_fixture
