#pragma once
#include "relay_service_fixture.hh"
#include "task_support.hh"
#include <http_asio.h>
#include <http_asio_tls.h>
#include <http_parser.h>
#include <sys/resource.h>

namespace relay_network_fixture {
namespace r = detent::relay;
namespace transport = detent::transport;
using relay_service_fixture::hex;
enum class FailurePoint { None, Launch, OpenBarrier, NextBarrier, Suite };
struct InjectedFailure final : std::exception {
	const char *what() const noexcept override { return "injected network group failure"; }
};
struct Load {
	std::size_t opened = 0, requested = 0, completed = 0, retries = 0, admitted = 0;
	FailurePoint injection = FailurePoint::None;
	std::array<task::cancel_source, 500> cancellation;
	std::exception_ptr failure;
	std::vector<std::int64_t> latency;
	std::uint64_t bytes = 0;
};
struct Response {
	class SHA256 hash;
	http_parser parser{};
	bool complete = false, headers = false;
	std::uint64_t bytes = 0;
	Response() {
		http_parser_init(&parser, HTTP_RESPONSE);
		parser.data = this;
	}
	static int body(http_parser *parser, const char *data, std::size_t size) {
		auto &self = *static_cast<Response *>(parser->data);
		self.hash.add(data, size);
		self.bytes += size;
		return 0;
	}
	static int head(http_parser *parser) {
		static_cast<Response *>(parser->data)->headers = true;
		return 0;
	}
	static int done(http_parser *parser) {
		static_cast<Response *>(parser->data)->complete = true;
		return 0;
	}
};
task::task<void> subscriber(t::ClientOptions options, Load &load, std::array<unsigned char, 32> expected,
							std::uint64_t length, unsigned index, task::cancel_token token) {
	auto &scheduler = *options.scheduler;
	t::SessionHandle session;
	for (unsigned retry = 0; !session.valid(); ++retry) {
		if (load.failure)
			co_return;
		try {
			session = co_await t::open_session(options, true, token);
		} catch (const asio::system_error &error) {
			if (retry == 200 ||
				(error.code() != asio::error::connection_reset && error.code() != asio::error::eof &&
				 error.code() != asio::ssl::error::stream_truncated))
				throw;
		} catch (const t::SessionResetError &error) {
			if (retry == 200 || (error.reason() != t::SessionResetReason::Busy &&
								 error.reason() != t::SessionResetReason::AdmissionFull))
				throw;
		}
		if (!session.valid()) {
			co_await pause(scheduler, std::chrono::milliseconds(10));
		}
	}
	++load.opened;
	if (index == 0 && load.injection == FailurePoint::OpenBarrier)
		throw InjectedFailure{};
	while (load.opened != 500 && !load.failure) {
		co_await pause(scheduler, std::chrono::milliseconds(1));
	}
	if (load.failure)
		co_return;
	const auto began = std::chrono::steady_clock::now();
	const auto body =
		"{\"version\":1,\"session\":\"" + hex(session.identity()) + "\",\"after\":0,\"wait_ms\":0}";
	const auto request = "POST /v2/distribution/session/next HTTP/1.1\r\nHost: localhost\r\n"
						 "Authorization: Bearer " +
						 options.token +
						 "\r\nContent-Type: application/json\r\n"
						 "Connection: close\r\nContent-Length: " +
						 std::to_string(body.size()) + "\r\n\r\n" + body;
	bool received = false;
	for (unsigned retry = 0; !received; ++retry) {
		if (load.failure)
			co_return;
		bool accepted = false;
		try {
			asio::ssl::stream<asio::ip::tcp::socket> socket(scheduler.executor(), *options.tls_context);
			socket.set_verify_mode(asio::ssl::verify_peer);
			socket.set_verify_callback(asio::ssl::host_name_verification(options.hostname));
			check(SSL_set_tlsext_host_name(socket.native_handle(), options.hostname.c_str()) == 1);
			co_await transport::await_io<void(asio::error_code)>(
				[&](auto handler) { socket.next_layer().async_connect(options.endpoint, std::move(handler)); }, token);
			co_await transport::await_io<void(asio::error_code)>(
				[&](auto handler) { socket.async_handshake(asio::ssl::stream_base::client, std::move(handler)); },
				token);
			co_await transport::await_io<void(asio::error_code, std::size_t)>(
				[&](auto handler) { asio::async_write(socket, asio::buffer(request), std::move(handler)); }, token);
			Response response;
			http_parser_settings settings{};
			settings.on_headers_complete = Response::head;
			settings.on_body = Response::body;
			settings.on_message_complete = Response::done;
			std::array<char, 16384> buffer;
			auto read = [&]() -> task::task<void> {
				const auto size = co_await transport::await_io<void(asio::error_code, std::size_t)>(
					[&](auto handler) { socket.async_read_some(asio::buffer(buffer), std::move(handler)); }, token);
				check(http_parser_execute(&response.parser, &settings, buffer.data(), size) == size);
				check(HTTP_PARSER_ERRNO(&response.parser) == HPE_OK);
			};
			while (!response.headers)
				co_await read();
			if (response.parser.status_code != 503) {
				if (response.parser.status_code != 200)
					throw std::runtime_error("network next status " +
											 std::to_string(response.parser.status_code));
				accepted = true;
				++load.requested;
				if (index == 0 && load.injection == FailurePoint::NextBarrier)
					throw InjectedFailure{};
				while (load.requested != 500 && !load.failure) {
					co_await pause(scheduler, std::chrono::milliseconds(1));
				}
				if (load.failure)
					co_return;
				while (!response.complete)
					co_await read();
				std::array<unsigned char, 32> actual;
				response.hash.getHash(actual.data());
				check(response.bytes == length && actual == expected);
				load.bytes += response.bytes;
				load.latency.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
										   std::chrono::steady_clock::now() - began)
										   .count());
				received = true;
			}
		} catch (const asio::system_error &error) {
			if (accepted ||
				(error.code() != asio::error::connection_reset && error.code() != asio::error::eof &&
				 error.code() != asio::ssl::error::stream_truncated))
				throw;
		}
		if (!received) {
			if (retry == 200)
				throw std::runtime_error("network admission retry bound");
			++load.retries;
			co_await pause(scheduler, std::chrono::milliseconds(10));
		}
	}
	try {
		co_await t::close_session(options, session, token);
	} catch (const asio::system_error &error) {
		if (error.code() != asio::error::connection_reset && error.code() != asio::error::eof &&
			error.code() != asio::ssl::error::stream_truncated)
			throw;
	} catch (const t::SessionResetError &error) {
		if (error.reason() != t::SessionResetReason::Expired &&
			error.reason() != t::SessionResetReason::Busy &&
			error.reason() != t::SessionResetReason::AdmissionFull)
			throw;
	}
}
inline std::int64_t cpu_microseconds(const rusage &usage) {
	return usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec + usage.ru_stime.tv_sec * 1000000 +
		   usage.ru_stime.tv_usec;
}
task::task<void> qualify(task::asio_scheduler &scheduler, std::shared_ptr<r::Bundle> bundle,
						 d::RetirementHandle<d::EncodedUpdate> expected,
						 std::shared_ptr<d::StorageLedger> account,
						 std::shared_ptr<asio::ssl::context> server_tls,
						 std::shared_ptr<asio::ssl::context> client_tls, asio::thread_pool &worker,
						 std::uint32_t records, FailurePoint injection = FailurePoint::None) {
	struct rlimit descriptors{};
	if (::getrlimit(RLIMIT_NOFILE, &descriptors) != 0)
		throw std::runtime_error("network fixture cannot inspect descriptor limit");
	if (descriptors.rlim_cur < 4096) {
		if (descriptors.rlim_max < 4096)
			throw std::runtime_error("500-client network fixture requires a descriptor hard limit of 4096");
		descriptors.rlim_cur = 4096;
		if (::setrlimit(RLIMIT_NOFILE, &descriptors) != 0)
			throw std::runtime_error("network fixture cannot raise descriptor soft limit to 4096");
	}
	const auto &executor = scheduler.executor();
	if (injection == FailurePoint::Suite) {
		for (auto point : {FailurePoint::Launch, FailurePoint::OpenBarrier, FailurePoint::NextBarrier})
			co_await qualify(scheduler, bundle, expected, account, server_tls, client_tls, worker, records, point);
		co_return;
	}
	const auto target = expected->target();
	const std::array<std::string_view, 1> names{"app"};
	r::ConfiguredCredential credential;
	credential.authorization.principal.identity.fill('p');
	credential.authorization.principal.policy_epoch = 1;
	credential.authorization.grants.fill('g');
	credential.authorization.bundles[0] = 1;
	const std::string token = "0123456789abcdef0123456789abcdef";
	class SHA256 token_hash;
	token_hash.add(token.data(), token.size());
	token_hash.getHash(credential.token_hash.data());
	r::ServiceAccounts accounts{std::make_shared<d::StorageLedger>(account, 8 * 1024 * 1024),
								{std::make_shared<d::StorageLedger>(account, 8 * 1024 * 1024),
								 std::make_shared<d::StorageLedger>(account, 40 * 1024 * 1024),
								 std::make_shared<d::StorageLedger>(account, 8 * 1024 * 1024)}};
	auto registry = std::allocate_shared<r::Registry>(
		d::StorageAllocator<r::Registry>(accounts.metadata), accounts.metadata,
		std::array{r::RegistryBundle{target, credential.authorization.principal, names}},
		std::array{credential});
	const std::array bindings{r::SessionBinding{bundle, credential.authorization.principal, target}};
	auto runtime = std::allocate_shared<r::ServiceRuntime>(
		d::StorageAllocator<r::ServiceRuntime>(accounts.metadata), accounts, registry, bindings);
	r::Service service(runtime);
	http::HttpAsioService listener(service, 2, 1024, http::tls_connection_factory(server_tls), 1024);
	http::AsioBindOptions bind;
	bind.address = "127.0.0.1";
	listener.set_bind_options(bind);
	listener.set_request_timeout(std::chrono::seconds(65));
	listener.start(0);
	const asio::ip::tcp::endpoint endpoint{asio::ip::address_v4::loopback(), listener.port()};
	class SHA256 hash;
	std::uint64_t length = 0;
	for (std::size_t ordinal = 0; ordinal < expected->fragments(); ++ordinal) {
		const auto frame = expected->frame(ordinal, true);
		hash.add(frame->view().data(), frame->size());
		length += frame->size();
	}
	std::array<unsigned char, 32> digest;
	hash.getHash(digest.data());
	Load load;
	load.injection = injection;
	load.latency.reserve(500);
	t::ClientOptions options;
	options.scheduler = &scheduler;
	options.endpoint = endpoint;
	options.hostname = "localhost";
	options.token = token;
	options.tls_context = client_tls;
	options.configured = target;
	options.namespaces = {"app"};
	options.limits.storage_account = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
	options.timeout = std::chrono::seconds(60);
	rusage before{}, after{};
	check(getrusage(RUSAGE_SELF, &before) == 0);
	const auto began = std::chrono::steady_clock::now();
	auto fail = [&](std::exception_ptr error) {
		if (!load.failure)
			load.failure = error;
	};
	try {
		for (unsigned index = 0; index < 500; ++index) {
			if (index == 17 && injection == FailurePoint::Launch)
				throw InjectedFailure{};
			task::spawn(task::scheduler_ref::of(scheduler),
						subscriber(options, load, digest, length, index, load.cancellation[index].token()),
						[&](std::exception_ptr error) {
							if (error)
								fail(error);
							++load.completed;
						});
			++load.admitted;
		}
	} catch (...) {
		fail(std::current_exception());
	}
	std::size_t peak_polls = 0;
	const auto deadline = began + std::chrono::seconds(180);
	bool cancelling = false;
	while (load.completed != load.admitted) {
		try {
			runtime->step(r::Sessions::Clock::now());
			peak_polls = std::max(peak_polls, runtime->status().requests[1]);
			if (std::chrono::steady_clock::now() >= deadline && !load.failure)
				throw std::runtime_error("network fan-out deadline");
		} catch (...) {
			fail(std::current_exception());
		}
		if (load.failure && !cancelling) {
			cancelling = true;
			runtime->close();
			for (std::size_t index = 0; index < load.admitted; ++index)
				load.cancellation[index].request();
		}
		try {
			co_await pause(scheduler, std::chrono::milliseconds(1));
		} catch (...) {
			// Cleanup cannot unwind across admitted original callbacks.
			std::terminate();
		}
	}

	const auto connections = listener.connection_stats();
	const auto peak_transfers = runtime->status().peak_transfers;
	runtime->close();
	std::atomic<bool> stopped{false};
	try {
		asio::post(worker, [&] {
			listener.stop();
			stopped.store(true);
		});
	} catch (...) {
		std::terminate();
	}
	while (!stopped.load() || !runtime->drained()) {
		runtime->step(r::Sessions::Clock::now());
		try {
			co_await pause(scheduler, std::chrono::milliseconds(1));
		} catch (...) {
			// Cleanup cannot unwind across admitted original callbacks.
			std::terminate();
		}
	}
	if (injection != FailurePoint::None) {
		check(load.failure && load.completed == load.admitted && runtime->drained());
		try {
			std::rethrow_exception(load.failure);
		} catch (const InjectedFailure &) {
			std::cout << "network group failure drained " << load.admitted << " admitted originals\n";
			co_return;
		}
	}
	if (load.failure)
		std::rethrow_exception(load.failure);
	check(load.opened == 500 && load.requested == 500 && load.latency.size() == 500);
	check(load.bytes == length * 500 && connections.high_water >= 500);
	check(getrusage(RUSAGE_SELF, &after) == 0);
	std::sort(load.latency.begin(), load.latency.end());
#ifdef __APPLE__
	const auto rss = after.ru_maxrss;
#else
	const auto rss = after.ru_maxrss * 1024;
#endif
	std::cout << "{\"network_subscribers\":500,\"records\":" << records
			  << ",\"coding\":\"zstd\",\"egress_bytes\":" << load.bytes
			  << ",\"cpu_us\":" << cpu_microseconds(after) - cpu_microseconds(before) << ",\"wall_ms\":"
			  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
																	   began)
					 .count()
			  << ",\"p50_us\":" << load.latency[249] << ",\"p99_us\":" << load.latency[494]
			  << ",\"process_peak_rss\":" << rss << ",\"control_peak\":" << account->peak()
			  << ",\"client_admitted_peak\":" << options.limits.storage_account->peak()
			  << ",\"peak_connections\":" << connections.high_water
			  << ",\"admission_retries\":" << load.retries << ",\"peak_polls\":" << peak_polls
			  << ",\"peak_transfers\":" << peak_transfers << "}\n";
}
} // namespace relay_network_fixture
