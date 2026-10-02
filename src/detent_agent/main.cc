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
#include "config.hh"
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
	if (!source_path && !source_http && opts.defaults_path.empty()) source_http = "127.0.0.1:" + std::to_string(detent_cli::kDefaultPort);
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

// Ties "is the daemon actually alive" to the same `flock` the daemon itself
// holds for its entire lifetime (acquire_pidfile(), below), instead of a
// raw pid read + kill(pid, 0): a plain pid check has a TOCTOU window where
// the daemon exits and the OS recycles that pid for an unrelated process
// between the check and a later signal. Returns std::nullopt if the
// pidfile is absent, or if it exists but nothing holds its lock (stale from
// a previous, uncleanly-stopped run) -- either way, not running.
std::optional<pid_t> locked_pid() {
	int fd = ::open(pid_path().c_str(), O_RDONLY);
	if (fd < 0) return std::nullopt; // no pidfile at all
	if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
		// We just acquired the lock ourselves -- nobody was holding it, so
		// no daemon is alive regardless of what pid the file happens to
		// name.
		::flock(fd, LOCK_UN);
		::close(fd);
		return std::nullopt;
	}
	// Lock is held by someone else: that's only ever the live daemon
	// (acquire_pidfile() holds it for its entire process lifetime and
	// nothing else in this codebase ever locks this file), so the pid this
	// file currently names is trustworthy to read right now.
	std::ifstream in(pid_path());
	pid_t pid = 0;
	if (in) in >> pid;
	::close(fd);
	return pid > 0 ? std::optional<pid_t>(pid) : std::nullopt;
}

#ifndef DETENT_READER
// Acquires an exclusive flock on the pidfile and writes our own pid into it.
// Returns the held fd (keep it open for the process lifetime -- closing
// releases the lock) or -1 if another instance already holds it.
int acquire_pidfile() {
	auto dir = runtime_dir();
	std::filesystem::create_directories(dir);
	struct stat st{};
	if (::lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::geteuid()) {
		throw std::runtime_error("runtime directory must be owned by the current user and not be a symlink: " + dir.string());
	}
	if (::chmod(dir.c_str(), g_system ? 0755 : 0700) != 0) throw std::runtime_error("cannot make runtime directory private: " + dir.string());
	int fd = ::open(pid_path().c_str(), O_CREAT | O_RDWR, g_system ? 0644 : 0600);
	if (fd < 0) throw std::runtime_error("cannot open pidfile: " + std::string(std::strerror(errno)));
	if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
		::close(fd);
		return -1; // already running (idempotent start)
	}
	if (::ftruncate(fd, 0) != 0) { /* best-effort */ }
	auto pid_str = std::to_string(::getpid());
	[[maybe_unused]] auto written = ::write(fd, pid_str.data(), pid_str.size());
	return fd;
}

detent_agent::Agent* g_agent = nullptr;

void handle_signal(int) {
	if (g_agent) g_agent->stop();
}

void install_signal_handlers() {
	::signal(SIGINT, handle_signal);
	::signal(SIGTERM, handle_signal);
}

int run_foreground(detent_agent::Options options, int ready_fd = -1) {
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
		if (ready_fd >= 0) ::close(ready_fd);
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

int cmd_start(const std::vector<std::string>& args, bool quiet = false) {
	if (g_system && has_flag(args, "--daemonize")) throw std::runtime_error("system services must run in the foreground");
	auto options = detent_agent::Options{shm_path(), build_client_options(args), g_system};

	if (auto pid = locked_pid()) {
		if (!quiet && !has_flag(args, "--quiet")) std::printf("detent-agent: already running (pid %d)\n", *pid);
		return 0; // idempotent
	}

	if (!has_flag(args, "--daemonize")) {
		int pidfd = acquire_pidfile();
		if (pidfd < 0) {
			if (!quiet && !has_flag(args, "--quiet")) std::printf("detent-agent: already running\n");
			return 0;
		}
		return run_foreground(std::move(options));
	}

	// Single fork + setsid: enough to detach from the launching shell/session
	// and reparent to init on exit, without a controlling terminal to
	// reacquire. The parent waits for initialization and receives startup errors.
	int ready[2];
	if (::pipe(ready) != 0) throw std::runtime_error("cannot create startup pipe");
	pid_t pid = fork();
	if (pid < 0) {
		::close(ready[0]);
		::close(ready[1]);
		std::perror("detent-agent: fork");
		return 1;
	}
	if (pid > 0) {
		::close(ready[1]);
		std::string error;
		char buffer[512];
		ssize_t count;
		while ((count = ::read(ready[0], buffer, sizeof(buffer))) != 0) {
			if (count < 0) {
				if (errno == EINTR) continue;
				::close(ready[0]);
				throw std::runtime_error("cannot read startup result");
			}
			error.append(buffer, static_cast<std::size_t>(count));
		}
		::close(ready[0]);
		if (!error.empty()) throw std::runtime_error(error);
		return 0;
	}

	::close(ready[0]);
	try {
		::setsid();
		int pidfd = acquire_pidfile();
		if (pidfd < 0) {
			_exit(0); // another instance won the race; not an error
		}

		auto log_path = runtime_dir() / "agent.log";
		int log_fd = ::open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
		if (log_fd >= 0) {
			::dup2(log_fd, STDOUT_FILENO);
			::dup2(log_fd, STDERR_FILENO);
			if (log_fd > 2) ::close(log_fd);
		}
		int devnull = ::open("/dev/null", O_RDONLY);
		if (devnull >= 0) {
			::dup2(devnull, STDIN_FILENO);
			if (devnull > 2) ::close(devnull);
		}

		_exit(run_foreground(std::move(options), ready[1]));
	} catch (const std::exception& e) {
		std::fprintf(stderr, "detent-agent: %s\n", e.what());
		[[maybe_unused]] auto written = ::write(ready[1], e.what(), std::strlen(e.what()));
		::close(ready[1]);
		_exit(1);
	}
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

int cmd_stop() {
	if (g_system) throw std::runtime_error("stop the system agent through its service manager");
	auto pid = locked_pid();
	if (!pid) {
		std::printf("not running\n");
		return 0;
	}
	::kill(*pid, SIGTERM);
	for (int i = 0; i < 50; ++i) { // up to ~5s for a graceful shutdown
		std::this_thread::sleep_for(100ms);
		// Re-derive liveness from the lock, not kill(pid, 0): if the pid we
		// signaled has already exited and the OS handed that number to an
		// unrelated process, kill(pid, 0) on the recycled pid would report
		// "still alive" and this loop would spin for no reason -- locked_pid()
		// instead asks "is anyone still holding the daemon's own lock",
		// which is unaffected by pid reuse.
		if (!locked_pid()) {
			std::printf("stopped\n");
			return 0;
		}
	}
	std::printf("detent-agent: pid %d did not exit in time\n", *pid);
	return 1;
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
			std::vector<std::string> start_args{daemon, "start", "--daemonize", "--quiet"};
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
	"  detent-agent start [--daemonize] [--defaults PATH] [--source-path PATH]\n"
	"                      [--source-address HOST:PORT] [--poll-interval-ms N]\n"
	"  detent-agent status\n"
	"  detent-agent stop\n"
	"  detent-agent restart [startup flags]\n"
	"  detent-agent --help | -h\n"
	"\n"
	"start runs in the foreground unless --daemonize is supplied.\n"
	"restart stops and starts a user agent in the background.\n"
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
	if (argc < 2) {
		std::fprintf(stderr, "%s", kUsage);
		return 2;
	}
	g_executable = argv[0];
	std::string command = argv[1];
	std::vector<std::string> args(argv + 2, argv + argc);

	if (command == "--help" || command == "-h" || command == "help") {
		std::fputs(kUsage, stdout);
		return 0;
	}

	try {
		g_system = has_flag(args, "--system");
		g_config = detent_agent::load_config(args);
#ifndef DETENT_READER
		g_logging_settings = detent_logging::parse(args, g_config);
#endif
		g_runtime_dir = setting(args, "--runtime-dir", "DETENT_RUNTIME_DIR");

#ifndef DETENT_READER
		if (command == "start") return cmd_start(args);
		if (command == "status") return cmd_status();
		if (command == "stop") return cmd_stop();
		if (command == "restart") {
			if (cmd_stop() != 0) return 1;
			args.emplace_back("--daemonize");
			return cmd_start(args);
		}
#else
		if (command == "get") return cmd_get(args);
#endif
	} catch (const std::exception& e) {
		std::fprintf(stderr, "detent-agent: %s\n", e.what());
		return 1;
	}

	std::fprintf(stderr, "detent-agent: unknown command '%s'\n", command.c_str());
	return 2;
}
