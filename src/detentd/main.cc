// detentd: the durable authority daemon. Wires detent::authority::Service into
// http::HttpAsioService and runs until stopped.
//
// Usage:
//   detentd --authority CONFIG [--daemonize] [--port N] [--reactors N]
//           [--runtime-dir PATH] [--system] [--config PATH]
//   detentd --status [--authority CONFIG] [--port N]
#include "../detent_authority/service.hh"
#include "../detent_authority/transport/https_client.hh"
#include "../detent_cli/config.hh"
#include "../detent_cli/daemon.hh"
#include "../detent_cli/logging.hh"
#include <http_asio.h>
#include <http_asio_tls.h>
#include <unistd.h>

#include <atomic>
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
#include <thread>
#include <vector>

namespace {
std::atomic<bool> g_stop{false};
// The publisher's connection cap and in-flight limit.
constexpr std::size_t kPublisherConnections = 1024;
void handle_signal(int) { g_stop.store(true); }

const char* kUsage =
	"Logging: --log-level LEVEL, -v/-vv, --verbosity 0..2, --color auto|always|never|truecolor|256|16\n"
	"usage: detentd --authority CONFIG [--daemonize] [--runtime-dir PATH] [--config PATH]\n"
	"               [--port N] [--reactors N] [--system]\n"
	"       detentd --status [--authority CONFIG] [--runtime-dir PATH] [--port N]\n"
	"\n"
	"detentd: the durable authority daemon. It serves the authority configured in\n"
	"CONFIG (bounded namespace transactions, producer sessions, snapshot and delta\n"
	"delivery) and replicates through Raft when CONFIG lists peers.\n"
	"\n"
	"With no --port: port 8317 (0 binds a free port and logs it).\n"
	"--system selects the system config and runtime directories.\n"
	"Config: detentd.json in the same platform directories as agent.json.\n"
	"Fields: authority, port, reactors, runtime_dir, log_level, verbosity, color. Paths are config-relative.\n"
	"Flags override DETENTD_PORT, DETENTD_REACTORS, DETENTD_RUNTIME_DIR, which override config.\n"
	"\n"
	"--status probes the local instance over plain HTTP, or over HTTPS with CA and name\n"
	"verification when CONFIG's publisher uses TLS, and prints its pid and health.\n";

int positive_int(const std::string& value, int maximum, const std::string& field) {
	int number = 0;
	auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
	if (error != std::errc{} || end != value.data() + value.size() || number <= 0 || number > maximum) {
		throw std::runtime_error(field + " must be an integer between 1 and " + std::to_string(maximum));
	}
	return number;
}

int port_number(const std::string& value) {
	int number = 0;
	auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
	if (error != std::errc{} || end != value.data() + value.size() || number < 0 || number > 65535) {
		throw std::runtime_error("port must be an integer between 0 and 65535 (0 binds a free port)");
	}
	return number;
}

std::string probe_health(const std::optional<detent::authority::ServiceOptions>& authority, unsigned short port) {
	auto address = authority ? authority->publisher_bind : asio::ip::make_address(detent_cli::kLoopbackHost);
	if (address.is_unspecified()) address = address.is_v4()
		? asio::ip::address(asio::ip::address_v4::loopback())
		: asio::ip::address(asio::ip::address_v6::loopback());
	const asio::ip::tcp::endpoint endpoint{address, port};
	if (authority && authority->publisher_tls) {
		return detent::transport::https_health(
			{"", "", authority->publisher_tls->certificate_authority}, endpoint, authority->publisher_name);
	}
	return detent::transport::http_health(endpoint, address.to_string());
}

int run(const std::vector<std::string>& input_args) {
	auto args = input_args;
	detent_cli::reject_removed_options("detentd", args);
	using enum detent_cli::FieldKind;
	auto file = detent_cli::load_config(args, "detentd.json", {
		{"log_level", {"--log-level", String}},
		{"verbosity", {"--verbosity", NonnegativeInt}},
		{"color", {"--color", String}},
		{"runtime_dir", {"--runtime-dir", Path}},
		{"port", {"--port", NonnegativeInt}},
		{"authority", {"--authority", Path}},
		{"reactors", {"--reactors", PositiveInt}},
	});
	auto settings = std::move(file.values);
	auto logging_settings = detent_logging::parse(args, settings);
	for (const auto& [flag, env] : {
		std::pair{"--runtime-dir", "DETENTD_RUNTIME_DIR"},
		std::pair{"--port", "DETENTD_PORT"},
		std::pair{"--reactors", "DETENTD_REACTORS"},
	}) {
		if (const char* value = std::getenv(env); value && *value) settings[flag] = value;
	}
	bool system = false, detach = false, status = false;
	std::size_t i = 0;
	auto next = [&](const std::string& flag) {
		if (i == args.size() || args[i].starts_with("--")) throw std::runtime_error(flag + " requires a value");
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
		} else if (arg == "--authority" || arg == "--port" || arg == "--reactors" || arg == "--runtime-dir") {
			settings[arg] = next(arg);
		} else {
			throw std::runtime_error("unknown argument: " + arg);
		}
	}
	auto port = static_cast<unsigned short>(settings.contains("--port")
		? port_number(settings.at("--port")) : detent_cli::kDefaultPort);
	auto reactors = settings.contains("--reactors") ? positive_int(settings.at("--reactors"), 65535, "reactors") : 4;
	if (system && detach) throw std::runtime_error("system services must run in the foreground");
	if (status && detach) throw std::runtime_error("--status and --daemonize are mutually exclusive");
	if (!status && !settings.contains("--authority")) throw std::runtime_error("--authority CONFIG is required");
	auto runtime = settings.contains("--runtime-dir") ? std::filesystem::path(settings.at("--runtime-dir")) : detent_cli::default_runtime_dir(system);
	auto pid_path = runtime / ("detentd-" + std::to_string(port) + ".pid");
	std::optional<detent::authority::ServiceOptions> authority_configuration;
	if (settings.contains("--authority")) {
		authority_configuration = detent::authority::load_service_options(settings.at("--authority"));
	}
	if (status) {
		try {
			auto body = probe_health(authority_configuration, port);
			if (auto pid = detent_cli::locked_pid(pid_path)) std::printf("running (pid %d)\n", *pid);
			std::printf("%s\n", body.c_str());
			return 0;
		} catch (const std::exception& e) {
			std::printf("not reachable: %s\n", e.what());
			return 1;
		}
	}
	return detent_cli::run_daemon(detach, runtime / ("detentd-" + std::to_string(port) + ".log"), [&](const detent_cli::Ready& ready) {
		detent_cli::ProcessLock process_lock(pid_path, system);
		if (!process_lock.acquired()) { std::printf("detentd: already running\n"); return 0; }

		detent_logging::Session logging(logging_settings);
		try {
			auto authority_options = std::move(*authority_configuration);
			const auto authority_network_timeout = authority_options.timeout;
			const auto authority_bind = authority_options.publisher_bind.to_string();
			const char* authority_mode = authority_options.peers ? "fixed-voter" : "single-voter";
			const bool publisher_https = authority_options.publisher_tls.has_value();
			http::HttpAsioService::ConnectionFactory connection;
			if (publisher_https) {
				auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
				reactor::tls::configure(*context, *authority_options.publisher_tls, reactor::tls::Role::Server);
				connection = http::tls_connection_factory(std::move(context));
			}
			const std::size_t body_cap = authority_options.node.application.request_bytes;
			detent::authority::Service authority(std::move(authority_options));
			// One value for the in-flight limit and the connection cap, so http's gate never refuses a
			// request before Detent's own admission does (D4).
			http::HttpAsioService service(authority, reactors, kPublisherConnections, std::move(connection),
				kPublisherConnections);
			service.set_request_timeout(authority_network_timeout);
			authority.observe_connections(service.connection_observer());
			service.set_max_body(body_cap);
			service.enable_conditional();

			std::signal(SIGINT, handle_signal);
			std::signal(SIGTERM, handle_signal);
			// A write to a socket whose peer has reset must fail with EPIPE, not kill the daemon.
			std::signal(SIGPIPE, SIG_IGN);

			http::AsioBindOptions bind;
			bind.address = authority_bind;
			service.set_bind_options(bind);
			service.start(port);
			L_INFO("detentd: durable {} authority listening with {} on {}:{}", authority_mode,
				publisher_https ? "HTTPS" : "HTTP", authority_bind, service.port());
			// Ready means a leader accepts /v2/open; the startup timeout bounds this wait.
			while (!authority.has_leader() && !g_stop.load()) {
				std::this_thread::sleep_for(std::chrono::milliseconds{5});
			}
			ready();

			while (!g_stop.load()) {
				std::this_thread::sleep_for(std::chrono::milliseconds{200});
			}

			// Teardown (D15): end the owner waits, stop the loops, then close and destroy the roots
			// of every scheduler with every io_context alive; destruction follows in reverse.
			L_NOTICE("detentd: shutting down");
			authority.begin_shutdown();
			service.stop();
			authority.drain();
			authority.owner().stop();
			service.close();
			authority.owner().close();
			service.destroy_roots();
			authority.owner().destroy_roots();
			return 0;
		} catch (const std::exception& e) {
			L_ERR("detentd: {}", e.what());
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
	} catch (const std::exception& e) {
		std::fprintf(stderr, "detentd: %s\n", e.what());
		return 1;
	}
}
