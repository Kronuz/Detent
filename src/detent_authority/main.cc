// detent-authority: the durable authority daemon. Wires detent::authority::Service into
// http::HttpAsioService and runs it through task::lifecycle (docs/mechanisms/server-composition.md).
//
// Usage:
//   detent-authority --authority CONFIG [--daemonize] [--port N] [--reactors N]
//                    [--runtime-dir PATH] [--system] [--config PATH]
//   detent-authority --status [--authority CONFIG] [--port N]
#include "service.hh"
#include "transport/https_client.hh"
#include "../detent_cli/config.hh"
#include "../detent_cli/daemon.hh"
#include "../detent_cli/logging.hh"
#include <http_asio.h>
#include <http_asio_tls.h>
#include <lifecycle.hh>
#include <task_asio.hh>
#include <sysexits.h>
#include <unistd.h>

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// A bad option value exits EX_USAGE.
struct usage_error : std::runtime_error { using std::runtime_error::runtime_error; };

// The shared option defaults (server-composition note, Options).
constexpr std::size_t kDefaultMaxClients = 1024, kDefaultMaxInFlight = 1024;
constexpr std::chrono::milliseconds kDefaultIdleTimeout{75'000}, kDefaultShutdownTimeout{35'000};
constexpr std::chrono::milliseconds kShutdownGrace{5'000}; // fixed

const char* kUsage =
	"Logging: --log-level LEVEL, -v/-vv, --verbosity 0..2, --color auto|always|never|truecolor|256|16\n"
	"usage: detent-authority --authority CONFIG [--daemonize] [--runtime-dir PATH] [--config PATH]\n"
	"                        [--port N] [--reactors N] [--max-clients N] [--max-in-flight N]\n"
	"                        [--idle-timeout-ms MS] [--shutdown-timeout-ms MS] [--system]\n"
	"       detent-authority --status [--authority CONFIG] [--runtime-dir PATH] [--port N]\n"
	"                        [--upstream-ca FILE]\n"
	"\n"
	"detent-authority: the durable authority daemon. It serves the authority configured in\n"
	"CONFIG (bounded namespace transactions, producer sessions, snapshot and delta\n"
	"delivery) and replicates through Raft when CONFIG lists peers.\n"
	"\n"
	"With no --port: port 8317 (0 binds a free port and logs it).\n"
	"--system selects the system config and runtime directories.\n"
	"Config: detent-authority.json in the same platform directories as agent.json.\n"
	"Fields: authority, port, reactors, max_clients, max_in_flight, idle_timeout_ms,\n"
	"shutdown_timeout_ms, upstream_ca, runtime_dir, log_level, verbosity, color. Paths are config-relative.\n"
	"Flags override DETENT_AUTHORITY_PORT, DETENT_AUTHORITY_REACTORS, DETENT_AUTHORITY_RUNTIME_DIR, which override config.\n"
	"\n"
	"--max-clients bounds open connections (default 1024); --max-in-flight bounds admitted requests per\n"
	"reactor (default 1024); --idle-timeout-ms closes an idle connection (default 75000, 0 = never).\n"
	"SIGINT or SIGTERM stops: new requests are refused, requests in flight finish and a response being\n"
	"written gets the 5 s grace (at most the bound), within --shutdown-timeout-ms (default 35000).\n"
	"A second signal changes nothing yet; a third flushes and dies.\n"
	"\n"
	"--status probes the local instance over plain HTTP, or over HTTPS with CA and name\n"
	"verification when CONFIG's publisher uses TLS, and prints its pid and health.\n"
	"--upstream-ca names the CA the probe verifies against, instead of the publisher's.\n";

std::uint64_t unsigned_value(const std::string& field, const std::string& value, std::uint64_t minimum, std::uint64_t maximum) {
	std::uint64_t number = 0;
	auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
	if (value.empty() || error != std::errc{} || end != value.data() + value.size() || number < minimum || number > maximum) {
		throw usage_error(field + " must be an integer between " + std::to_string(minimum) + " and " + std::to_string(maximum));
	}
	return number;
}

std::chrono::milliseconds duration_ms(const std::string& field, const std::string& value, std::uint64_t minimum) {
	return std::chrono::milliseconds(unsigned_value(field, value, minimum, std::uint64_t(kMaxDuration.count())));
}

std::string probe_health(const std::optional<detent::authority::ServiceOptions>& authority, unsigned short port,
	const std::optional<std::string>& upstream_ca) {
	auto address = authority ? authority->publisher_bind : asio::ip::make_address(detent_cli::kLoopbackHost);
	if (address.is_unspecified()) address = address.is_v4()
		? asio::ip::address(asio::ip::address_v4::loopback())
		: asio::ip::address(asio::ip::address_v6::loopback());
	const asio::ip::tcp::endpoint endpoint{address, port};
	if (authority && authority->publisher_tls) {
		return detent::transport::https_health(
			{"", "", upstream_ca ? *upstream_ca : authority->publisher_tls->certificate_authority}, endpoint,
			authority->publisher_name);
	}
	return detent::transport::http_health(endpoint, address.to_string());
}

// The authority state as a component. stop() ends every owner wait with its truthful reply and
// stops admitting, before the http component stops; drain() waits until every task has settled
// and left the owner's loop.
struct AuthorityComponent {
	detent::authority::Service& authority;
	task::task<void> start(task::cancel_token) { co_return; }
	void stop() { authority.begin_shutdown(); }
	task::task<std::vector<std::string>> drain(const task::stop_bound&) {
		authority.drain();
		co_return std::vector<std::string>{};
	}
	void flush() {}
	std::string name() const { return "authority"; }
};

// The http listener: start() binds and serves; drain() runs http's stop, which cancels the requests
// in flight (their responses are still written), gives them the grace, then forces.
struct HttpComponent {
	http::HttpAsioService& service;
	unsigned short port;
	std::function<void()> listening;
	task::task<void> start(task::cancel_token) {
		service.start(port);
		listening();
		co_return;
	}
	void stop() {}
	task::task<std::vector<std::string>> drain(const task::stop_bound&) {
		service.stop();
		co_return std::vector<std::string>{};
	}
	void flush() {}
	std::string name() const { return "http"; }
};

// A node that alone is a quorum waits for its own leader before it is ready; with other voters it
// is ready at once, and /v1/health reports ready only when a leader is known. A stop or a runtime
// failure ends the wait without ready, and the parent receives the cause.
struct LeaderComponent {
	detent::authority::Service& authority;
	bool lone_quorum;
	task::task<void> start(task::cancel_token stop) {
		while (lone_quorum && !authority.has_leader() && !stop.requested()) {
			if (auto reason = authority.failure()) {
				throw std::runtime_error("authority stopped before a leader was elected: " + *reason);
			}
			co_await task::sleep_for(std::chrono::milliseconds{5}, stop);
		}
	}
	void stop() {}
	task::task<std::vector<std::string>> drain(const task::stop_bound&) { co_return std::vector<std::string>{}; }
	void flush() {}
	std::string name() const { return "leader"; }
};

int run(const std::vector<std::string>& input_args) {
	auto args = input_args;
	detent_cli::reject_removed_options("detent-authority", args);
	using enum detent_cli::FieldKind;
	auto file = detent_cli::load_config(args, "detent-authority.json", {
		{"log_level", {"--log-level", String}},
		{"verbosity", {"--verbosity", NonnegativeInt}},
		{"color", {"--color", String}},
		{"runtime_dir", {"--runtime-dir", Path}},
		{"port", {"--port", NonnegativeInt}},
		{"authority", {"--authority", Path}},
		{"reactors", {"--reactors", PositiveInt}},
		{"max_clients", {"--max-clients", PositiveInt}},
		{"max_in_flight", {"--max-in-flight", NonnegativeInt}},
		{"idle_timeout_ms", {"--idle-timeout-ms", NonnegativeInt}},
		{"shutdown_timeout_ms", {"--shutdown-timeout-ms", PositiveInt}},
		{"upstream_ca", {"--upstream-ca", Path}},
	});
	auto settings = std::move(file.values);
	auto logging_settings = detent_logging::parse(args, settings);
	for (const auto& [flag, env] : {
		std::pair{"--runtime-dir", "DETENT_AUTHORITY_RUNTIME_DIR"},
		std::pair{"--port", "DETENT_AUTHORITY_PORT"},
		std::pair{"--reactors", "DETENT_AUTHORITY_REACTORS"},
	}) {
		if (const char* value = std::getenv(env); value && *value) settings[flag] = value;
	}
	bool system = false, detach = false, status = false;
	std::size_t i = 0;
	auto next = [&](const std::string& flag) {
		if (i == args.size() || args[i].starts_with("--")) throw usage_error(flag + " requires a value");
		return args[i++];
	};
	while (i < args.size()) {
		auto arg = args[i++];
		if (arg == "--system") {
			system = true;
		} else if (arg == "--daemonize") {
			detach = true;
		} else if (arg == "--status") {
			status = true;
		} else if (arg == "--config") {
			(void)next(arg); // already loaded above
		} else if (arg == "--authority" || arg == "--port" || arg == "--reactors" || arg == "--runtime-dir" ||
			arg == "--max-clients" || arg == "--max-in-flight" || arg == "--idle-timeout-ms" ||
			arg == "--shutdown-timeout-ms" || arg == "--upstream-ca") {
			settings[arg] = next(arg);
		} else {
			throw usage_error("unknown argument: " + arg);
		}
	}
	auto value = [&](const char* flag) -> std::optional<std::string> {
		if (auto it = settings.find(flag); it != settings.end()) return it->second;
		return std::nullopt;
	};
	auto port = static_cast<unsigned short>(unsigned_value("port", value("--port").value_or(std::to_string(detent_cli::kDefaultPort)), 0, 65535));
	auto reactors = static_cast<std::size_t>(unsigned_value("reactors", value("--reactors").value_or("4"), 1, 65535));
	auto max_clients = static_cast<std::size_t>(unsigned_value("max_clients", value("--max-clients").value_or(std::to_string(kDefaultMaxClients)), 1, 1u << 24));
	auto max_in_flight = static_cast<std::size_t>(unsigned_value("max_in_flight", value("--max-in-flight").value_or(std::to_string(kDefaultMaxInFlight)), 0, 1u << 24));
	auto idle_timeout = value("--idle-timeout-ms") ? duration_ms("idle_timeout_ms", *value("--idle-timeout-ms"), 0) : kDefaultIdleTimeout;
	auto shutdown_timeout = value("--shutdown-timeout-ms") ? duration_ms("shutdown_timeout_ms", *value("--shutdown-timeout-ms"), 1) : kDefaultShutdownTimeout;
	if (system && detach) throw usage_error("system services must run in the foreground");
	if (status && detach) throw usage_error("--status and --daemonize are mutually exclusive");
	if (!status && !settings.contains("--authority")) throw usage_error("--authority CONFIG is required");
	auto runtime = settings.contains("--runtime-dir") ? std::filesystem::path(settings.at("--runtime-dir")) : detent_cli::default_runtime_dir(system);
	auto pid_path = runtime / ("detent-authority-" + std::to_string(port) + ".pid");
	std::optional<detent::authority::ServiceOptions> authority_configuration;
	if (settings.contains("--authority")) {
		authority_configuration = detent::authority::load_service_options(settings.at("--authority"));
	}
	if (status) {
		try {
			auto body = probe_health(authority_configuration, port, value("--upstream-ca"));
			if (auto pid = detent_cli::locked_pid(pid_path)) std::printf("running (pid %d)\n", *pid);
			std::printf("%s\n", body.c_str());
			return 0;
		} catch (const std::exception& e) {
			std::printf("not reachable: %s\n", e.what());
			return 1;
		}
	}
	return detent_cli::run_daemon(detach, runtime / ("detent-authority-" + std::to_string(port) + ".log"), [&](const detent_cli::Ready& ready) {
		detent_cli::ProcessLock process_lock(pid_path, system);
		if (!process_lock.acquired()) { std::printf("detent-authority: already running\n"); std::fflush(stdout); return 0; }

		task::stop_signals signals; // the first thread after the fork; SIGINT and SIGTERM stop
		// A write to a socket whose peer has reset must fail with EPIPE, not kill the daemon.
		std::signal(SIGPIPE, SIG_IGN);
		detent_logging::Session logging(logging_settings);
		try {
			auto authority_options = std::move(*authority_configuration);
			const auto authority_network_timeout = authority_options.timeout;
			const auto authority_bind = authority_options.publisher_bind.to_string();
			const char* authority_mode = authority_options.peers ? "fixed-voter" : "single-voter";
			const bool lone_quorum = !authority_options.peers || authority_options.peers->addresses.empty();
			const bool publisher_https = authority_options.publisher_tls.has_value();
			http::HttpAsioService::ConnectionFactory connection;
			if (publisher_https) {
				auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
				reactor::tls::configure(*context, *authority_options.publisher_tls, reactor::tls::Role::Server);
				connection = http::tls_connection_factory(std::move(context));
			}
			const std::size_t body_cap = authority_options.node.application.request_bytes;
			detent::authority::Service authority(std::move(authority_options));
			// max_in_flight and max_clients default to Detent's own admission limit (1024), so http's
			// gate never refuses a request before Detent's admission does (D4).
			http::HttpAsioService service(authority, reactors, max_in_flight, std::move(connection), max_clients);
			service.set_request_timeout(authority_network_timeout);
			service.set_idle_timeout(idle_timeout);
			service.set_shutdown_grace(std::min(kShutdownGrace, shutdown_timeout));
			authority.observe_connections(service.connection_observer());
			service.set_max_body(body_cap);
			service.enable_conditional();

			http::AsioBindOptions bind;
			bind.address = authority_bind;
			service.set_bind_options(bind);

			// The control loop runs on this thread; the authority's owner loop keeps its own thread.
			asio::io_context io(1);
			task::asio_scheduler loop(io.get_executor());
			AuthorityComponent authority_component{authority};
			HttpComponent http_component{service, port, [&] {
				L_INFO("detent-authority: durable {} authority listening with {} on {}:{}", authority_mode,
					publisher_https ? "HTTPS" : "HTTP", authority_bind, service.port());
			}};
			LeaderComponent leader_component{authority, lone_quorum};
			const task::stop_timing timing{shutdown_timeout, std::min<std::chrono::nanoseconds>(kShutdownGrace, shutdown_timeout)};
			task::stopped result;
			task::spawn(task::scheduler_ref::of(loop),
				task::lifecycle(loop, signals, timing, ready, authority_component, http_component, leader_component),
				[&](std::exception_ptr error, std::optional<task::stopped> stopped) {
					result = stopped ? std::move(*stopped) : task::stopped{error};
					io.stop();
				});
			io.run(); // the lifecycle holds the loop's work guard until every flush returned

			// Teardown (D15): the lifecycle stopped and drained the components; close and destroy the
			// roots of every scheduler with every io_context alive; destruction follows in reverse.
			L_NOTICE("detent-authority: shutting down");
			for (const auto& line : result.unfinished) L_WARNING("detent-authority: unfinished: {}", line);
			authority.owner().stop();
			service.close();
			authority.owner().close();
			loop.close();
			service.destroy_roots();
			authority.owner().destroy_roots();
			loop.destroy_roots();
			if (result.failure) std::rethrow_exception(result.failure);
			if (!result.ready && detach) throw std::runtime_error("stopped before startup completed");
			if (!result.unfinished.empty()) return EX_TEMPFAIL;
			return 0;
		} catch (const std::exception& e) {
			L_ERR("detent-authority: {}", e.what());
			throw;
		}
	});
}

} // namespace

int main(int argc, char** argv) {
	std::vector<std::string> args(argv + 1, argv + argc);
	for (const auto& arg : args) {
		if (arg == "--help" || arg == "-h") {
			std::fputs(kUsage, stdout);
			return 0;
		}
	}
	try {
		return run(args);
	} catch (const usage_error& e) {
		std::fprintf(stderr, "detent-authority: %s\n", e.what());
		return EX_USAGE;
	} catch (const std::exception& e) {
		std::fprintf(stderr, "detent-authority: %s\n", e.what());
		return 1;
	}
}
