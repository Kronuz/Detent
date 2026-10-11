#include "../detent_cli/config.hh"
#include "../detent_cli/logging.hh"
#include "detent_relay/configuration.hh"
#include "detent_transport/https_client.hh"
#include "host.hh"
#include <csignal>
#include <cstdio>

int main(int argc, char **argv) {
	try {
		std::vector<std::string> args(argv + 1, argv + argc);
		const auto logging = detent_logging::parse(args, {});
		bool system = false, detach = false, status = false;
		std::optional<std::filesystem::path> configuration, runtime;
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto &arg = args[index];
			if (arg == "--help" || arg == "-h") {
				std::puts("Usage: detent-relay [--config FILE] [--system] [--daemonize] [--status]\n"
						  "                    [--runtime-dir DIR] [--log-level LEVEL] [--color MODE]\n"
						  "Runs in the foreground by default. --config selects private relay JSON.");
				return 0;
			}
			if (arg == "--system")
				system = true;
			else if (arg == "--daemonize")
				detach = true;
			else if (arg == "--status")
				status = true;
			else if (arg == "--config" || arg == "--runtime-dir") {
				if (++index == args.size() || args[index].empty())
					throw std::invalid_argument("missing relay option value");
				(arg == "--config" ? configuration : runtime) = args[index];
			} else
				throw std::invalid_argument("unknown relay option: " + arg);
		}
		if (system && detach)
			throw std::invalid_argument("system services must run in the foreground");
		if (status && detach)
			throw std::invalid_argument("--status and --daemonize are mutually exclusive");
		const auto config =
			configuration
				? *configuration
				: (system ? detent_cli::system_config_dir() : detent_cli::user_config_dir()) / "relay.json";
		const auto directory = runtime ? *runtime : detent_cli::default_runtime_dir(system);
		const auto pid = directory / "relay.pid";
		if (status) {
			const auto options = detent::relay::load_host_options(config, system);
			auto endpoint = options.endpoint;
			if (endpoint.address().is_unspecified())
				endpoint.address(endpoint.address().is_v4()
									 ? asio::ip::address(asio::ip::address_v4::loopback())
									 : asio::ip::address(asio::ip::address_v6::loopback()));
			const auto body =
				detent::transport::https_health({"", "", options.certificate_authority}, endpoint,
												options.hostname, std::chrono::seconds(2), 256 * 1024);
			if (const auto running = detent_cli::locked_pid(pid))
				std::printf("running (pid %d)\n", *running);
			std::printf("%s\n", body.c_str());
			return 0;
		}
		std::signal(SIGPIPE, SIG_IGN);
		return detent_cli::run_daemon(detach, directory / "relay.log", [&](const detent_cli::Ready &ready) {
			detent_cli::ProcessLock lock(pid, system);
			if (!lock.acquired()) {
				std::puts("detent-relay: already running");
				return 0;
			}
			detent_logging::Session session(logging);
			return detent::relay::run_host(config, system, ready);
		});
	} catch (const std::exception &error) {
		std::fprintf(stderr, "detent-relay: %s\n", error.what());
		return 1;
	}
}
