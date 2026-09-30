// detent-agent: the host-local daemon and its own CLI in one binary --
// `start` runs the daemon, `get` is the fast client (a fresh process each
// call, reading directly from shared memory -- no socket, no daemon round
// trip), `status`/`stop` manage the daemon's lifecycle via its pidfile and
// signals.
//
// Usage:
//   detent-agent start [--daemonize] [--defaults PATH] [--source-path PATH]
//                       [--source-http HOST:PORT] [--poll-interval-ms N]
//   detent-agent status
//   detent-agent stop
//   detent-agent get <name> [--hash VAL] [--switch VAL] [--default VAL]
//
// Configuration for `start` (and for `get`'s auto-start-on-cold-call) comes
// from flags, falling back to environment variables so a `get` invocation
// doesn't need to repeat the daemon's whole configuration on every call:
//   DETENT_RUNTIME_DIR   shm/pid/log directory
//                        (default $XDG_RUNTIME_DIR/detent, else ~/.detent/run)
//   DETENT_DEFAULTS_PATH required to (auto-)start the daemon
//   DETENT_SOURCE_HTTP   host:port  (mutually exclusive with the next)
//   DETENT_SOURCE_PATH   local snapshot file path
//   DETENT_POLL_INTERVAL_MS
#include "agent.hh"
#include "detent_http/http_source.hh"
#include "shm_reader.hh"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
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

std::filesystem::path runtime_dir() {
	if (const char* v = std::getenv("DETENT_RUNTIME_DIR"); v && *v) return v;
	if (const char* v = std::getenv("XDG_RUNTIME_DIR"); v && *v) return std::filesystem::path(v) / "detent";
	if (const char* home = std::getenv("HOME"); home && *home) return std::filesystem::path(home) / ".detent" / "run";
	return std::filesystem::temp_directory_path() / "detent-run";
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

// Builds InitOptions from CLI flags, falling back to the DETENT_* env vars.
detent::InitOptions build_client_options(const std::vector<std::string>& args) {
	detent::InitOptions opts;

	auto defaults = arg_value(args, "--defaults");
	const char* env_defaults = std::getenv("DETENT_DEFAULTS_PATH");
	if (defaults) {
		opts.defaults_path = *defaults;
	} else if (env_defaults && *env_defaults) {
		opts.defaults_path = env_defaults;
	} else {
		throw std::runtime_error("detent-agent: no defaults path (--defaults or DETENT_DEFAULTS_PATH)");
	}

	auto source_path = arg_value(args, "--source-path");
	auto source_http = arg_value(args, "--source-http");
	const char* env_source_path = std::getenv("DETENT_SOURCE_PATH");
	const char* env_source_http = std::getenv("DETENT_SOURCE_HTTP");
	if (!source_path && env_source_path && *env_source_path) source_path = env_source_path;
	if (!source_http && env_source_http && *env_source_http) source_http = env_source_http;

	if (source_path && source_http) {
		throw std::runtime_error("detent-agent: --source-path and --source-http are mutually exclusive");
	}
	if (source_path) {
		opts.source_path = *source_path;
	} else if (source_http) {
		auto colon = source_http->rfind(':');
		if (colon == std::string::npos) throw std::runtime_error("detent-agent: --source-http needs host:port");
		auto host = source_http->substr(0, colon);
		auto port = static_cast<std::uint16_t>(std::atoi(source_http->c_str() + colon + 1));
		opts.source_poll = detent_http::make_http_poll_source(host, port);
		opts.source_id = "http";
	}

	auto poll_ms = arg_value(args, "--poll-interval-ms");
	const char* env_poll_ms = std::getenv("DETENT_POLL_INTERVAL_MS");
	if (poll_ms) {
		opts.poll_interval = std::chrono::milliseconds{std::atoi(poll_ms->c_str())};
	} else if (env_poll_ms && *env_poll_ms) {
		opts.poll_interval = std::chrono::milliseconds{std::atoi(env_poll_ms)};
	}

	return opts;
}

// Returns the running daemon's pid if the pidfile names a live process,
// std::nullopt otherwise (no pidfile, unreadable, or stale). Used for
// `status`, where the worst case of a stale/reused pid is a wrong status
// line, not signaling an unrelated process.
std::optional<pid_t> live_pid() {
	std::ifstream in(pid_path());
	if (!in) return std::nullopt;
	pid_t pid = 0;
	in >> pid;
	if (pid <= 0) return std::nullopt;
	if (::kill(pid, 0) != 0) return std::nullopt; // no such process
	return pid;
}

// Ties "is the daemon actually alive" to the same `flock` the daemon itself
// holds for its entire lifetime (acquire_pidfile(), below), instead of a
// raw pid read + kill(pid, 0): a plain pid check has a TOCTOU window where
// the daemon exits and the OS recycles that pid for an unrelated process
// between the check and a later signal. Returns std::nullopt if the
// pidfile is absent, or if it exists but nothing holds its lock (stale from
// a previous, uncleanly-stopped run) -- either way, not running.
std::optional<pid_t> locked_pid() {
	int fd = ::open(pid_path().c_str(), O_RDWR);
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

// Acquires an exclusive flock on the pidfile and writes our own pid into it.
// Returns the held fd (keep it open for the process lifetime -- closing
// releases the lock) or -1 if another instance already holds it.
int acquire_pidfile() {
	std::filesystem::create_directories(runtime_dir());
	int fd = ::open(pid_path().c_str(), O_CREAT | O_RDWR, 0600);
	if (fd < 0) return -1;
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

int run_foreground(detent_agent::Options options) {
	detent_agent::Agent agent(std::move(options));
	g_agent = &agent;
	install_signal_handlers();
	agent.serve(); // blocks until stop()
	return 0;
}

int cmd_start(const std::vector<std::string>& args) {
	auto options = detent_agent::Options{shm_path(), build_client_options(args)};

	if (auto pid = live_pid()) {
		std::printf("detent-agent: already running (pid %d)\n", *pid);
		return 0; // idempotent
	}

	if (!has_flag(args, "--daemonize")) {
		int pidfd = acquire_pidfile();
		if (pidfd < 0) {
			std::printf("detent-agent: already running\n");
			return 0;
		}
		return run_foreground(std::move(options));
	}

	// Single fork + setsid: enough to detach from the launching shell/session
	// and reparent to init on exit, without a controlling terminal to
	// reacquire. The parent returns immediately (idempotent backgrounding).
	pid_t pid = fork();
	if (pid < 0) {
		std::perror("detent-agent: fork");
		return 1;
	}
	if (pid > 0) {
		return 0; // parent: return control to the caller right away
	}

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

	_exit(run_foreground(std::move(options)));
}

int cmd_status() {
	auto pid = live_pid();
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
	if (!reader) {
		// Cold call: no daemon has ever published yet. Auto-start it (config
		// comes from env vars only here -- see file header) and retry
		// briefly while it comes up and does its first publish.
		try {
			auto options = detent_agent::Options{shm_path(), build_client_options({})};
			(void)options; // validated only; the child re-derives it from env
		} catch (const std::exception& e) {
			if (default_val) {
				std::printf("%s\n", default_val->c_str());
				return 0;
			}
			std::fprintf(stderr, "detent-agent: no daemon running and cannot auto-start: %s\n", e.what());
			return 1;
		}

		pid_t pid = fork();
		if (pid == 0) {
			::setsid();
			int pidfd = acquire_pidfile();
			if (pidfd < 0) _exit(0);
			int devnull = ::open("/dev/null", O_RDWR);
			if (devnull >= 0) {
				::dup2(devnull, STDIN_FILENO);
				::dup2(devnull, STDOUT_FILENO);
				::dup2(devnull, STDERR_FILENO);
				if (devnull > 2) ::close(devnull);
			}
			_exit(run_foreground(detent_agent::Options{shm_path(), build_client_options({})}));
		}
		for (int i = 0; i < 20; ++i) { // up to ~2s for the daemon to come up and publish
			std::this_thread::sleep_for(100ms);
			reader = detent_agent::ShmReader::open(shm_path());
			if (reader) break;
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

const char* kUsage =
	"detent-agent: host-local daemon + CLI for a warm, shared detent::Client\n"
	"\n"
	"usage:\n"
	"  detent-agent start [--daemonize] [--defaults PATH] [--source-path PATH]\n"
	"                      [--source-http HOST:PORT] [--poll-interval-ms N]\n"
	"  detent-agent status\n"
	"  detent-agent stop\n"
	"  detent-agent get <name> [--hash VAL] [--switch VAL] [--default VAL]\n"
	"  detent-agent --help | -h\n"
	"\n"
	"`start` runs the daemon in the foreground unless --daemonize forks it into\n"
	"the background. `get` reads directly from the shared-memory snapshot the\n"
	"daemon publishes -- no socket, no daemon involvement per call. If no\n"
	"daemon has ever published yet, `get` auto-starts one using the same config\n"
	"resolution as `start` (flags here, then the environment variables below),\n"
	"then retries briefly while it comes up.\n"
	"\n"
	"environment variables (fallback when a flag above is omitted):\n"
	"  DETENT_RUNTIME_DIR       shm/pid/log directory\n"
	"                           (default $XDG_RUNTIME_DIR/detent, else ~/.detent/run)\n"
	"  DETENT_DEFAULTS_PATH     required to (auto-)start the daemon\n"
	"  DETENT_SOURCE_HTTP       host:port  (mutually exclusive with the next)\n"
	"  DETENT_SOURCE_PATH       local snapshot file path\n"
	"  DETENT_POLL_INTERVAL_MS\n";

} // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "%s", kUsage);
		return 2;
	}
	std::string command = argv[1];
	std::vector<std::string> args(argv + 2, argv + argc);

	if (command == "--help" || command == "-h" || command == "help") {
		std::fputs(kUsage, stdout);
		return 0;
	}

	try {
		if (command == "start") return cmd_start(args);
		if (command == "status") return cmd_status();
		if (command == "stop") return cmd_stop();
		if (command == "get") return cmd_get(args);
	} catch (const std::exception& e) {
		std::fprintf(stderr, "detent-agent: %s\n", e.what());
		return 1;
	}

	std::fprintf(stderr, "detent-agent: unknown command '%s'\n", command.c_str());
	return 2;
}
