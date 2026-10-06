// Shared CLI entry point for detent-agent lifecycle and detent reads.
// DETENT_READER excludes daemon and HTTP code from the reader binary.
//
// Configuration: flags override environment variables, then agent.json.
// Baked defaults are optional; source-only agents start with an empty snapshot.
#ifndef DETENT_READER
#include "agent.hh"
#include "../detent_cli/logging.hh"
#include "detent_http/http_source.hh"
#endif
#include "../detent_cli/daemon.hh"
#include "config.hh"
#include "detent_http/address.hh"
#include "shm_reader.hh"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <limits>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

detent_agent::Config g_config;
#ifndef DETENT_READER
detent_logging::Settings g_logging_settings;
#endif
std::optional<std::string> g_runtime_dir;
bool g_system = false;
std::string g_executable;

std::filesystem::path runtime_dir() {
	if (g_runtime_dir) return *g_runtime_dir;
	return detent_agent::default_runtime_dir(g_system);
}

std::filesystem::path shm_path() { return runtime_dir() / "snapshot.shm"; }
std::filesystem::path pid_path() { return runtime_dir() / "agent.pid"; }

std::optional<std::string> arg_value(const std::vector<std::string>& args, const std::string& flag) {
	for (std::size_t i = 0; i + 1 < args.size(); ++i) {
		if (args[i] == flag) return args[i + 1];
	}
	return std::nullopt;
}

bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
	for (const auto& a : args) if (a == flag) return true;
	return false;
}

std::optional<std::string> setting(const std::vector<std::string>& args, const std::string& flag, const char* env) {
	if (auto value = arg_value(args, flag)) return value;
	if (const char* value = std::getenv(env); value && *value) return value;
	if (auto it = g_config.find(flag); it != g_config.end()) return it->second;
	return std::nullopt;
}

#ifndef DETENT_READER
int positive_int(const std::string& value, int maximum, const std::string& field) {
	int number = 0;
	auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
	if (error != std::errc{} || end != value.data() + value.size() || number <= 0 || number > maximum) {
		throw std::runtime_error(field + " must be an integer between 1 and " + std::to_string(maximum));
	}
	return number;
}

detent::InitOptions build_client_options(const std::vector<std::string>& args) {
	detent::InitOptions opts;
	if (auto defaults = setting(args, "--defaults", "DETENT_DEFAULTS_PATH")) opts.defaults_path = *defaults;

	// Source selection overrides the whole lower-priority source, so an HTTP
	// flag can replace a file source from config without an artificial conflict.
	auto source_path = arg_value(args, "--source-path");
	auto source_http = arg_value(args, "--source-address");
	if (!source_http) source_http = arg_value(args, "--source-http");
	if (!source_path && !source_http) {
		if (const char* v = std::getenv("DETENT_SOURCE_PATH"); v && *v) source_path = v;
		if (const char* v = std::getenv("DETENT_SOURCE_ADDRESS"); v && *v) source_http = v;
		else if (const char* v = std::getenv("DETENT_SOURCE_HTTP"); v && *v) source_http = v;
		if (!source_path && !source_http) {
			if (auto it = g_config.find("--source-path"); it != g_config.end()) source_path = it->second;
			if (auto it = g_config.find("--source-address"); it != g_config.end()) source_http = it->second;
		}
	}
	if (!source_path && !source_http && opts.defaults_path.empty()) source_http = std::string(detent_http::kLoopbackHost) + ":" + std::to_string(detent_cli::kDefaultPort);
	if (source_path && source_http) throw std::runtime_error("--source-path and --source-address are mutually exclusive");
	if (source_path) {
		opts.source_path = *source_path;
	} else if (source_http) {
		auto colon = source_http->rfind(':');
		if (colon == std::string::npos || colon == 0) throw std::runtime_error("--source-address needs host:port");
		auto host = source_http->substr(0, colon);
		auto port = static_cast<std::uint16_t>(positive_int(source_http->substr(colon + 1), 65535, "source_address port"));
		opts.source_poll = detent_http::make_http_poll_source(host, port);
		opts.source_id = "http";
	}
	if (auto poll_ms = setting(args, "--poll-interval-ms", "DETENT_POLL_INTERVAL_MS")) {
		opts.poll_interval = std::chrono::milliseconds{positive_int(*poll_ms, std::numeric_limits<int>::max(), "poll_interval_ms")};
	}
	return opts;
}

#endif

std::optional<pid_t> locked_pid() { return detent_cli::locked_pid(pid_path()); }

#ifndef DETENT_READER
detent_agent::Agent* g_agent = nullptr;

void handle_signal(int) {
	if (g_agent) g_agent->stop();
}

void install_signal_handlers() {
	::signal(SIGINT, handle_signal);
	::signal(SIGTERM, handle_signal);
}

int run_foreground(detent_agent::Options options, const detent_cli::Ready& ready) {
	detent_logging::Session logging(g_logging_settings);
	bool have_source = options.client_options.source_path || options.client_options.source_poll;
	options.on_publish = [](const detent::Snapshot& snap, bool published) {
		if (published) { L_DEBUG("detent-agent: published snapshot version {}, {} knobs", snap.version, snap.knobs.size()); }
		else { L_ERR("detent-agent: snapshot version {} exceeds shared-memory capacity", snap.version); }
	};
	options.on_metrics = [have_source, connected = false, first = true, failures_seen = std::uint64_t{0}, swaps_seen = std::uint64_t{0}, warned = false](const detent::Metrics& metrics) mutable {
		const bool source_connected = metrics.source_connected.load(std::memory_order_relaxed);
		const auto failures = metrics.refresh_failures.load(std::memory_order_relaxed);
		const auto swaps = metrics.snapshot_swaps.load(std::memory_order_relaxed);
		if (have_source && (first || source_connected != connected)) {
			if (source_connected) { L_NOTICE("detent-agent: source connected"); }
			else { L_WARNING("detent-agent: source unavailable; serving last published snapshot"); }
		}
		if (swaps != swaps_seen) warned = false;
		if (failures > failures_seen) {
			L(warned ? LOG_DEBUG : LOG_WARNING, "detent-agent: source refresh failed; total failures {}", failures);
			warned = true;
		}
		failures_seen = failures;
		swaps_seen = swaps;
		L(MAX_LOG_PRIORITY, "detent-agent: source connected {}, snapshot swaps {}, refresh failures {}", source_connected, swaps, failures);
		first = false;
		connected = source_connected;
	};
	try {
		detent_agent::Agent agent(std::move(options));
		g_agent = &agent;
		install_signal_handlers();
		L_INFO("detent-agent: started (pid {}), runtime {}", ::getpid(), runtime_dir().string());
		ready();
		agent.serve();
		L_NOTICE("detent-agent: shutting down");
		g_agent = nullptr;
		return 0;
	} catch (const std::exception& e) {
		g_agent = nullptr;
		L_ERR("detent-agent: {}", e.what());
		throw;
	}
}

int run_agent(const std::vector<std::string>& args) {
	const bool detach = has_flag(args, "--daemonize");
	if (g_system && detach) throw std::runtime_error("system services must run in the foreground");
	auto options = detent_agent::Options{shm_path(), build_client_options(args), g_system};
	return detent_cli::run_daemon(detach, runtime_dir() / "agent.log", [&](const detent_cli::Ready& ready) {
		detent_cli::ProcessLock lock(pid_path(), g_system);
		if (!lock.acquired()) {
			if (!has_flag(args, "--quiet")) std::printf("detent-agent: already running\n");
			return 0;
		}
		return run_foreground(std::move(options), ready);
	});
}

int cmd_status() {
	auto pid = locked_pid();
	if (!pid) {
		std::printf("not running\n");
		return 1;
	}
	std::printf("running (pid %d)\n", *pid);

	// No socket ping: read the same shm the daemon publishes to, directly.
	auto reader = detent_agent::ShmReader::open(shm_path());
	if (reader) {
		auto st = reader->status();
		std::printf("OK version=%llu knob_count=%u\n", (unsigned long long)st.accepted_version, st.knob_count);
	}
	return 0;
}

#endif

#ifdef DETENT_READER
// Tries eval() first (bool), then get() (int) if eval() reports the knob is
// actually an int -- the CLI doesn't know a knob's type up front the way
// the old socket protocol's server-side dispatch did, since there's no
// server in the loop to ask anymore.
std::optional<std::string> try_read(
	const detent_agent::ShmReader& reader, const std::string& name,
	const std::optional<std::string>& hash_val, const std::optional<std::string>& switch_val,
	detent::KnobError* out_error) {
	auto b = reader.eval(name, hash_val, switch_val);
	if (b) return b.value() ? "true" : "false";
	if (b.error().category != detent::ErrorCategory::WrongType) {
		if (out_error) *out_error = b.error();
		return std::nullopt;
	}
	auto i = reader.get(name, switch_val);
	if (i) return std::to_string(i.value());
	if (out_error) *out_error = i.error();
	return std::nullopt;
}

int cmd_get(const std::vector<std::string>& args) {
	if (args.empty()) {
		std::fprintf(stderr, "usage: detent-agent get <name> [--hash VAL] [--switch VAL] [--default VAL]\n");
		return 2;
	}
	std::string name = args[0];
	auto hash_val = arg_value(args, "--hash");
	auto switch_val = arg_value(args, "--switch");
	auto default_val = arg_value(args, "--default");

	auto reader = detent_agent::ShmReader::open(shm_path());
	if (!reader || !locked_pid()) {
		if (g_system) {
			if (default_val) { std::printf("%s\n", default_val->c_str()); return 0; }
			throw std::runtime_error("system agent is not running; start it through its service manager");
		}
		try {
			std::filesystem::path executable(g_executable);
			auto daemon = executable.has_parent_path()
				? (std::filesystem::absolute(executable).parent_path() / "detent-agent").string()
				: std::string("detent-agent");
			std::vector<std::string> start_args{daemon, "--daemonize", "--quiet"};
			for (std::size_t i = 1; i < args.size(); ++i) {
				if (args[i] == "--hash" || args[i] == "--switch" || args[i] == "--default") { ++i; continue; }
				start_args.push_back(args[i]);
			}
			std::vector<char*> argv;
			for (auto& arg : start_args) argv.push_back(arg.data());
			argv.push_back(nullptr);
			auto child = ::fork();
			if (child < 0) throw std::runtime_error("cannot launch detent-agent");
			if (child == 0) {
				::execvp(daemon.c_str(), argv.data());
				std::fprintf(stderr, "detent: cannot execute %s: %s\n", daemon.c_str(), std::strerror(errno));
				_exit(127);
			}
			int status = 0;
			while (::waitpid(child, &status, 0) < 0) {
				if (errno != EINTR) throw std::runtime_error("cannot wait for detent-agent");
			}
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) throw std::runtime_error("detent-agent startup failed");
		} catch (const std::exception& e) {
			if (default_val) {
				std::printf("%s\n", default_val->c_str());
				return 0;
			}
			std::fprintf(stderr, "detent-agent: cannot auto-start: %s\n", e.what());
			return 1;
		}
		for (int i = 0; i < 20; ++i) {
			reader = detent_agent::ShmReader::open(shm_path());
			if (reader && locked_pid()) break;
			std::this_thread::sleep_for(100ms);
		}
	}

	if (!reader) {
		if (default_val) {
			std::printf("%s\n", default_val->c_str());
			return 0;
		}
		std::fprintf(stderr, "detent-agent: no daemon publish found\n");
		return 1;
	}

	if (reader->status().accepted_version == 0 && reader->status().knob_count == 0) {
		for (int i = 0; i < 20; ++i) {
			if (!locked_pid()) break;
			auto status = reader->status();
			if (status.accepted_version > 0 || status.knob_count > 0) break;
			std::this_thread::sleep_for(100ms);
		}
		if (reader->status().accepted_version == 0 && reader->status().knob_count == 0) {
			if (default_val) {
				std::printf("%s\n", default_val->c_str());
				return 0;
			}
			std::fprintf(stderr, "detent-agent: no snapshot received from source within 2s; check that the configured distributor is running and has a published snapshot\n");
			return 1;
		}
	}

	detent::KnobError error{detent::ErrorCategory::Uninitialized, "", ""};
	auto result = try_read(*reader, name, hash_val, switch_val, &error);
	if (result) {
		std::printf("%s\n", result->c_str());
		return 0;
	}
	if (default_val) {
		std::printf("%s\n", default_val->c_str());
		return 0;
	}
	std::fprintf(stderr, "ERR %s\n", error.what().c_str());
	return 1;
}

#endif

#ifdef DETENT_READER
const char* kUsage =
	"usage: detent get NAME [--hash VAL] [--switch VAL] [--default VAL]\n"
	"                  [--config PATH] [--defaults PATH] [--source-address HOST:PORT]\n"
	"                  [--source-path PATH] [--runtime-dir PATH] [--system]\n"
	"       detent help | --help\n"
	"User reads auto-start detent-agent; system reads attach to the managed service.\n"
	"Config: agent.json in the standard Detent config directory.\n"
	"Flags override environment variables, which override config.\n";
#else
const char* kUsage =
	"Logging: --log-level LEVEL, -v/-vv, --verbosity 0..2, --color auto|always|never\n"
	"detent-agent: host-local daemon + CLI for a warm, shared detent::Client\n"
	"\n"
	"usage:\n"
	"  detent-agent [--daemonize] [--defaults PATH] [--source-path PATH]\n"
	"                      [--source-address HOST:PORT] [--poll-interval-ms N]\n"
	"  detent-agent --status\n"
	"  detent-agent --help | -h\n"
	"\n"
	"Runs in the foreground unless --daemonize is supplied.\n"
	"Use signals or the service manager to stop/restart.\n"
	"Use detent get NAME to read or auto-start a user agent.\n"
	"\n"
	"Configuration: --config PATH, else $XDG_CONFIG_HOME/detent/agent.json\n"
	"(Linux/FreeBSD: ~/.config/detent; macOS: ~/Library/Application Support/Detent).\n"
	"System fallback: /etc/detent (FreeBSD: /usr/local/etc/detent; macOS: /Library/Application Support/Detent).\n"
	"JSON fields:\n"
	"source_address, source_path, defaults_path, runtime_dir, poll_interval_ms.\n"
	"Paths in config are relative to the config file. Defaults are optional;\n"
	"defaults.json beside agent.json is discovered automatically if present.\n"
	"--source-http is a legacy alias for --source-address (HTTP transport).\n"
	"Flags override environment variables, which override config settings.\n"
	"--runtime-dir PATH selects the runtime directory for any command.\n"
	"--system uses system config and /run/detent (BSD/macOS: /var/run/detent).\n"
	"System agents run in the foreground; get does not start unmanaged services.\n"
	"environment variables (optional):\n"
	"  DETENT_RUNTIME_DIR       shm/pid/log directory\n"
	"                           (default $XDG_RUNTIME_DIR/detent, else $TMPDIR/detent-<uid>)\n"
	"  DETENT_DEFAULTS_PATH     optional baked defaults file\n"
	"  DETENT_SOURCE_ADDRESS    host:port (default 127.0.0.1:8317 without defaults)\n"
	"  DETENT_SOURCE_HTTP       legacy alias for DETENT_SOURCE_ADDRESS\n"
	"  DETENT_SOURCE_PATH       local snapshot file path\n"
	"  DETENT_POLL_INTERVAL_MS\n";

#endif

} // namespace

int main(int argc, char** argv) {
	g_executable = argv[0];
#ifdef DETENT_READER
	if (argc < 2) { std::fprintf(stderr, "%s", kUsage); return 2; }
	std::string command = argv[1];
	std::vector<std::string> args(argv + 2, argv + argc);
	if (command == "--help" || command == "-h" || command == "help") { std::fputs(kUsage, stdout); return 0; }
#else
	std::vector<std::string> args(argv + 1, argv + argc);
	if (has_flag(args, "--help") || has_flag(args, "-h")) { std::fputs(kUsage, stdout); return 0; }
#endif
	try {
		g_system = has_flag(args, "--system");
		g_config = detent_agent::load_config(args);
#ifndef DETENT_READER
		g_logging_settings = detent_logging::parse(args, g_config);
		for (std::size_t i = 0; i < args.size(); ++i) {
			const auto& arg = args[i];
			if (arg == "--system" || arg == "--daemonize" || arg == "--status" || arg == "--quiet") continue;
			if (arg == "--defaults" || arg == "--source-path" || arg == "--source-address" || arg == "--source-http" || arg == "--poll-interval-ms" || arg == "--runtime-dir" || arg == "--config") {
				if (++i == args.size() || args[i].starts_with('-')) throw std::runtime_error(arg + " requires a value");
				continue;
			}
			throw std::runtime_error("unknown argument: " + arg);
		}
#endif
		g_runtime_dir = setting(args, "--runtime-dir", "DETENT_RUNTIME_DIR");
#ifndef DETENT_READER
		if (has_flag(args, "--status")) {
			if (has_flag(args, "--daemonize")) throw std::runtime_error("--status and --daemonize are mutually exclusive");
			return cmd_status();
		}
		return run_agent(args);
#else
		if (command == "get") return cmd_get(args);
		throw std::runtime_error("unknown command: " + command);
#endif
	} catch (const std::exception& e) {
		std::fprintf(stderr, "detent: %s\n", e.what());
		return 1;
	}
}
