// The distributor executable: wires Distributor into http::HttpAsioService
// and runs until stopped.
//
// Usage:
//   detentd <port> <persist_path>
//       [--reactors N] [--workers N]
//       [--cluster <token> <multicast_port> <multicast_group> <interface>
//                  <self_host:port> <peer_host:port> [<peer_host:port> ...]]
//
// Example (three same-host replicas forming a Raft cluster over loopback
// multicast, each told its OWN advertised HTTP endpoint plus every peer's):
//   detentd 8081 /tmp/jk-a.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083
//   detentd 8082 /tmp/jk-b.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8082 127.0.0.1:8081 127.0.0.1:8083
//   detentd 8083 /tmp/jk-c.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8083 127.0.0.1:8081 127.0.0.1:8082
#include "detentd.hh"
#ifdef DETENT_WITH_AUTHORITY
#include "../detent_authority/service.hh"
#endif
#include "../detent_cli/logging.hh"
#include "../detent_cli/config.hh"
#include "../detent_cli/daemon.hh"
#include "detent_http/http_client.hh"
#include <unistd.h>

#include <http_asio.h>

#include <atomic>
#include <chrono>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {
std::atomic<bool> g_stop{false};
void handle_signal(int) { g_stop.store(true); }

const char* kUsage =
	"Logging: --log-level LEVEL, -v/-vv, --verbosity 0..2, --color auto|always|never\n"
	"usage: detentd [--daemonize | --status] [--runtime-dir PATH] [<port> <persist_path>] [--config PATH]\n"
	"                [--authority PATH] (opt-in durable loopback single-voter mode)\n"
	"                [--port N] [--persist-path PATH] [--reactors N] [--workers N]\n"
	"                [--cluster <token> <multicast_port> <multicast_group>\n"
	"                           <interface> <self_host:port> <peer_host:port> ...]\n"
	"                [--raft-timing <heartbeat_timeout> <election_init>\n"
	"                               <election_min> <election_max>]\n"
	"\n"
	"detentd: the HTTP distribution-layer daemon -- serves GET/PUT/PATCH\n"
	"/v1/snapshot to clients, persists to <persist_path>, and optionally\n"
	"forms a Raft cluster with peer detentd instances over UDP multicast:\n"
	"only the elected leader accepts writes (a follower answers with the\n"
	"leader's address instead), and a write isn't reported accepted until a\n"
	"quorum of the cluster has it.\n"
	"\n"
	"With no arguments: port 8317, snapshot.json in the user state directory.\n"
	"--system selects system config and /var/lib/detent (BSD/macOS: /var/db/detent).\n"
	"Config: detentd.json in the same platform directories as agent.json.\n"
	"Fields: port, persist_path, reactors, workers, runtime_dir, log_level, verbosity, color. Paths are config-relative.\n"
	"Flags/positional arguments override DETENTD_PORT, DETENTD_PERSIST_PATH,\n"
	"DETENTD_REACTORS, DETENTD_WORKERS, which override config.\n"
	"Linux/FreeBSD state: $XDG_STATE_HOME/detent or ~/.local/state/detent.\n"
	"macOS state: ~/Library/Application Support/Detent/state.\n"
	"\n"
	"--raft-timing overrides cluster::Raft's default timing (1.0/4.0/10.0/\n"
	"30.0 seconds) -- e.g. for a fast end-to-end test harness; every node\n"
	"in one cluster should use the same values.\n"
	"\n"
	"example (three same-host replicas, each told its OWN advertised\n"
	"endpoint plus every peer's):\n"
	"  detentd 8081 /tmp/jk-a.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083\n"
	"  detentd 8082 /tmp/jk-b.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8082 127.0.0.1:8081 127.0.0.1:8083\n"
	"  detentd 8083 /tmp/jk-c.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8083 127.0.0.1:8081 127.0.0.1:8082\n";

int positive_int(const std::string& value, int maximum, const std::string& field) {
	int number = 0;
	auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
	if (error != std::errc{} || end != value.data() + value.size() || number <= 0 || number > maximum) {
		throw std::runtime_error(field + " must be an integer between 1 and " + std::to_string(maximum));
	}
	return number;
}

double positive_seconds(const std::string& value) {
	std::size_t used = 0;
	auto seconds = std::stod(value, &used);
	if (used != value.size() || !std::isfinite(seconds) || seconds <= 0) {
		throw std::runtime_error("Raft timing must be a positive number of seconds");
	}
	return seconds;
}

int run(const std::vector<std::string>& input_args) {
	auto args = input_args;
	using enum detent_cli::FieldKind;
	auto file = detent_cli::load_config(args, "detentd.json", {
		{"log_level", {"--log-level", String}},
		{"verbosity", {"--verbosity", NonnegativeInt}},
		{"color", {"--color", String}},
		{"runtime_dir", {"--runtime-dir", Path}},
		{"port", {"--port", PositiveInt}},
		{"persist_path", {"--persist-path", Path}},
		{"authority", {"--authority", Path}},
		{"reactors", {"--reactors", PositiveInt}},
		{"workers", {"--workers", PositiveInt}},
	});
	auto settings = std::move(file.values);
	auto logging_settings = detent_logging::parse(args, settings);
	for (const auto& [flag, env] : {
		std::pair{"--runtime-dir", "DETENTD_RUNTIME_DIR"},
		std::pair{"--port", "DETENTD_PORT"},
		std::pair{"--persist-path", "DETENTD_PERSIST_PATH"},
		std::pair{"--reactors", "DETENTD_REACTORS"},
		std::pair{"--workers", "DETENTD_WORKERS"},
	}) {
		if (const char* value = std::getenv(env); value && *value) settings[flag] = value;
	}
	detentd::Options options;
	cluster::RaftConfig raft_timing;
	bool system = false, detach = false, status = false;
	std::size_t i = 0;
	if (!args.empty() && !args[0].starts_with("-")) {
		if (args.size() < 2 || args[1].starts_with("--")) throw std::runtime_error("positional syntax requires both port and persist_path");
		settings["--port"] = args[0];
		settings["--persist-path"] = args[1];
		i = 2;
	}
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
		} else if (arg == "--authority" || arg == "--port" || arg == "--persist-path" || arg == "--reactors" || arg == "--workers" || arg == "--runtime-dir") {
			settings[arg] = next(arg);
		} else if (arg == "--raft-timing") {
			raft_timing.heartbeat_timeout = positive_seconds(next(arg));
			raft_timing.election_init = positive_seconds(next(arg));
			raft_timing.election_min = positive_seconds(next(arg));
			raft_timing.election_max = positive_seconds(next(arg));
		} else if (arg == "--cluster") {
			detentd::Options::Cluster cluster;
			cluster.token = next(arg);
			cluster.multicast_port = static_cast<unsigned short>(positive_int(next(arg), 65535, "multicast port"));
			cluster.multicast_group = next(arg);
			cluster.multicast_interface = next(arg);
			cluster.self_address = next(arg);
			while (i < args.size() && !args[i].starts_with("--")) cluster.peers.emplace_back(args[i++]);
			options.cluster = std::move(cluster);
		} else {
			throw std::runtime_error("unknown argument: " + arg);
		}
	}
	if (settings.contains("--authority") && (options.cluster || settings.contains("--persist-path"))) { throw std::runtime_error("authority mode uses its separate directory and cannot join legacy multicast"); }
	if (options.cluster) options.cluster->raft_config = raft_timing;
	auto port = static_cast<unsigned short>(settings.contains("--port")
		? positive_int(settings.at("--port"), 65535, "port") : detent_cli::kDefaultPort);
	auto reactors = settings.contains("--reactors") ? positive_int(settings.at("--reactors"), 65535, "reactors") : 4;
	auto workers = settings.contains("--workers") ? positive_int(settings.at("--workers"), 65535, "workers") : 8;
	if (settings.contains("--persist-path")) {
		options.persist_path = settings.at("--persist-path");
	} else {
		auto dir = detent_cli::default_state_dir(system);
		options.persist_path = dir / "snapshot.json";
	}
	if (system && detach) throw std::runtime_error("system services must run in the foreground");
	if (status && detach) throw std::runtime_error("--status and --daemonize are mutually exclusive");
	auto runtime = settings.contains("--runtime-dir") ? std::filesystem::path(settings.at("--runtime-dir")) : detent_cli::default_runtime_dir(system);
	auto pid_path = runtime / ("detentd-" + std::to_string(port) + ".pid");
	if (status) {
		try {
			detent_http::HttpConnection connection("127.0.0.1", port, std::chrono::milliseconds(2000));
			auto response = connection.get("/v1/health");
			if (response.status != 200) throw std::runtime_error("health endpoint failed");
			if (auto pid = detent_cli::locked_pid(pid_path)) std::printf("running (pid %d)\n", *pid);
			std::printf("%s\n", response.body.c_str());
			return 0;
		} catch (const std::exception& e) {
			std::printf("not reachable: %s\n", e.what());
			return 1;
		}
	}
	if (options.persist_path.empty()) throw std::runtime_error("persist_path must not be empty");
	return detent_cli::run_daemon(detach, runtime / ("detentd-" + std::to_string(port) + ".log"), [&](const detent_cli::Ready& ready) {
		detent_cli::ProcessLock process_lock(pid_path, system);
		if (!process_lock.acquired()) { std::printf("detentd: already running\n"); return 0; }
		if (!options.persist_path.parent_path().empty()) std::filesystem::create_directories(options.persist_path.parent_path());

	detent_logging::Session logging(logging_settings);
	options.on_accept = [](std::uint64_t version) { L_DEBUG("detentd: accepted snapshot version {}", version); };
	options.on_reject = [](detent::UpdateRejectReason reason) {
		static constexpr const char* names[] = {"I/O", "transport", "parse", "schema", "version", "oversize", "checksum"};
		L_WARNING("detentd: rejected publish ({})", names[static_cast<unsigned>(reason)]);
	};
	try {
		std::unique_ptr<http::HttpHandler> app;
		if (settings.contains("--authority")) {
#ifdef DETENT_WITH_AUTHORITY
			app = std::make_unique<detent::authority::Service>(detent::authority::load_service_options(settings.at("--authority")));
#else
			throw std::runtime_error("authority mode requires DETENT_BUILD_AUTHORITY=ON and reviewed cluster sources");
#endif
		 }
		else { app = std::make_unique<detentd::Distributor>(options); }
		http::HttpAsioService service(*app, reactors, workers, /*queue_limit=*/1024);
		service.enable_conditional(); // GET /v1/snapshot's automatic ETag / 304 short-circuit

		std::signal(SIGINT, handle_signal);
		std::signal(SIGTERM, handle_signal);
		// Belt-and-suspenders alongside the MSG_NOSIGNAL/SO_NOSIGPIPE guards on
		// detent_http's own send() calls (src/detent_http/http_client.cc): any
		// write to a socket whose peer has already reset the connection -- ours
		// or a future one in a dependency we don't control -- must not be able
		// to take the whole distributor down via the default SIGPIPE
		// disposition (process termination). EPIPE from write()'s return value
		// is the correct, already-handled failure path.
		std::signal(SIGPIPE, SIG_IGN);

		if (settings.contains("--authority")) { http::AsioBindOptions bind; bind.address = "127.0.0.1"; service.set_bind_options(bind); }
		service.start(port);
		ready();
		if (settings.contains("--authority")) { L_INFO("detentd: durable single-voter authority listening on 127.0.0.1:{}", port); }
		else { L_INFO("detentd: listening on :{}, persisting to {}", port, options.persist_path.string()); }

		while (!g_stop.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds{200});
		}

		L_NOTICE("detentd: shutting down");
		service.stop();
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
