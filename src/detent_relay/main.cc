#include "../detent_cli/config.hh"
#include "../detent_cli/logging.hh"
#include "detent_relay/configuration.hh"
#include "detent_transport/https_client.hh"
#include "host.hh"
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <sysexits.h>

namespace {
// A bad option value exits EX_USAGE.
struct usage_error : std::invalid_argument { using std::invalid_argument::invalid_argument; };

constexpr std::chrono::milliseconds kMaxDuration{std::chrono::hours(24 * 365)};

std::uint64_t unsigned_value(const std::string &field, const std::string &value, std::uint64_t minimum,
							 std::uint64_t maximum) {
	std::uint64_t number = 0;
	auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
	if (value.empty() || error != std::errc{} || end != value.data() + value.size() || number < minimum ||
		number > maximum)
		throw usage_error(field + " must be an integer between " + std::to_string(minimum) + " and " +
						  std::to_string(maximum));
	return number;
}
std::chrono::milliseconds duration_ms(const std::string &field, const std::string &value, std::uint64_t minimum) {
	return std::chrono::milliseconds(unsigned_value(field, value, minimum, std::uint64_t(kMaxDuration.count())));
}

const char *kUsage =
	"Usage: detent-relay [--config FILE] [--system] [--daemonize] [--status] [--runtime-dir DIR]\n"
	"                    [--log-level LEVEL] [--color MODE] [--max-clients N] [--max-in-flight N]\n"
	"                    [--idle-timeout-ms MS] [--shutdown-timeout-ms MS]\n"
	"Runs in the foreground by default. --config selects the relay's configuration, else\n"
	"detent-relay.json in the standard Detent config directory: process and role settings, and\n"
	"`secrets`, the path of the one private file (no group or other access) that holds the\n"
	"upstream token and the downstream credentials.\n"
	"--max-clients bounds open connections (default 1024); --max-in-flight bounds admitted requests\n"
	"per reactor (default: the same); --idle-timeout-ms closes an idle connection (default 75000,\n"
	"0 = never). SIGINT or SIGTERM stops: new requests are refused, requests in flight finish and a\n"
	"response being written gets the 5 s grace (at most the bound), within --shutdown-timeout-ms\n"
	"(default 35000). A second signal changes nothing yet; a third flushes and dies.\n"
	"Flags override the configuration.\n";

int run(const std::vector<std::string> &input) {
	auto args = input;
	using enum detent_cli::FieldKind;
	// One file holds process and role settings; load_config reads the process ones, load_host_options the role's.
	auto file = detent_cli::load_config(args, "detent-relay.json",
										{
											{"log_level", {"--log-level", String}},
											{"verbosity", {"--verbosity", NonnegativeInt}},
											{"color", {"--color", String}},
											{"runtime_dir", {"--runtime-dir", Path}},
											{"max_clients", {"--max-clients", PositiveInt}},
											{"max_in_flight", {"--max-in-flight", NonnegativeInt}},
											{"idle_timeout_ms", {"--idle-timeout-ms", NonnegativeInt}},
											{"shutdown_timeout_ms", {"--shutdown-timeout-ms", PositiveInt}},
										},
										true);
	auto values = std::move(file.values);
	const auto logging = detent_logging::parse(args, values);
	bool system = false, detach = false, status = false;
	for (std::size_t index = 0; index < args.size(); ++index) {
		const auto &arg = args[index];
		if (arg == "--system")
			system = true;
		else if (arg == "--daemonize")
			detach = true;
		else if (arg == "--status")
			status = true;
		else if (arg == "--config" || arg == "--runtime-dir" || arg == "--max-clients" ||
				 arg == "--max-in-flight" || arg == "--idle-timeout-ms" || arg == "--shutdown-timeout-ms") {
			if (++index == args.size() || args[index].starts_with("--"))
				throw usage_error(arg + " requires a value");
			if (arg != "--config")
				values[arg] = args[index];
		} else
			throw usage_error("unknown relay option: " + arg);
	}
	auto value = [&](const char *flag) -> std::optional<std::string> {
		if (const auto found = values.find(flag); found != values.end())
			return found->second;
		return std::nullopt;
	};
	detent::relay::HostSettings settings;
	if (const auto v = value("--max-clients"))
		settings.max_clients = unsigned_value("max_clients", *v, 1, 65536);
	if (const auto v = value("--max-in-flight"))
		settings.max_in_flight = unsigned_value("max_in_flight", *v, 0, 1u << 24);
	if (const auto v = value("--idle-timeout-ms"))
		settings.idle_timeout = duration_ms("idle_timeout_ms", *v, 0);
	if (const auto v = value("--shutdown-timeout-ms"))
		settings.shutdown_timeout = duration_ms("shutdown_timeout_ms", *v, 1);
	if (system && detach)
		throw usage_error("system services must run in the foreground");
	if (status && detach)
		throw usage_error("--status and --daemonize are mutually exclusive");
	if (file.path.empty())
		throw usage_error("no relay configuration: pass --config FILE or create detent-relay.json");
	const auto directory = value("--runtime-dir") ? std::filesystem::path(*value("--runtime-dir"))
												   : detent_cli::default_runtime_dir(system);
	const auto pid = directory / "relay.pid";
	if (status) {
		const auto options = detent::relay::load_host_options(file.path);
		auto endpoint = options.endpoint;
		if (endpoint.address().is_unspecified())
			endpoint.address(endpoint.address().is_v4() ? asio::ip::address(asio::ip::address_v4::loopback())
														: asio::ip::address(asio::ip::address_v6::loopback()));
		const auto body = detent::transport::https_health({"", "", options.certificate_authority}, endpoint,
														  options.hostname, std::chrono::seconds(2), 256 * 1024);
		if (const auto running = detent_cli::locked_pid(pid))
			std::printf("running (pid %d)\n", *running);
		std::printf("%s\n", body.c_str());
		return 0;
	}
	settings.configuration = file.path;
	settings.detach = detach;
	return detent_cli::run_daemon(detach, directory / "relay.log", [&](const detent_cli::Ready &ready) {
		detent_cli::ProcessLock lock(pid, system);
		if (!lock.acquired()) {
			std::puts("detent-relay: already running");
			return 0;
		}
		task::stop_signals signals; // the first thread after the fork; SIGINT and SIGTERM stop
		std::signal(SIGPIPE, SIG_IGN);
		detent_logging::Session session(logging);
		return detent::relay::run_host(settings, ready, signals);
	});
}
} // namespace

int main(int argc, char **argv) {
	std::vector<std::string> args(argv + 1, argv + argc);
	for (const auto &arg : args) {
		if (arg == "--help" || arg == "-h") {
			std::fputs(kUsage, stdout);
			return 0;
		}
	}
	try {
		return run(args);
	} catch (const usage_error &error) {
		std::fprintf(stderr, "detent-relay: %s\n", error.what());
		return EX_USAGE;
	} catch (const std::exception &error) {
		std::fprintf(stderr, "detent-relay: %s\n", error.what());
		return 1;
	}
}
