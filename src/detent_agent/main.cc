// Shared CLI entry point for detent-agent lifecycle and detent reads.
// DETENT_READER excludes daemon and HTTP code from the reader binary.
//
// Configuration: flags override environment variables, then agent.json.
#ifndef DETENT_READER
#include "../detent_cli/logging.hh"
#ifdef DETENT_WITH_MANAGED_AGENT
#include "managed_host.hh"
#endif
#endif
#include "../detent_cli/daemon.hh"
#include "config.hh"
#include "reader_capacity.hh"
#include "detent/identity.hh"
#include "../detent_cli/config.hh"
#include "detent/reader.hh"
#include "detent_local/generation.hh"
#include "detent_local/reader.hh"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

detent_agent::Config g_config;
#ifndef DETENT_READER
detent_logging::Settings g_logging_settings;
#endif
std::optional<std::string> g_runtime_dir;
std::optional<std::string> g_managed_config;
std::optional<std::string> g_generation_dir;
bool g_system = false;
std::string g_executable;

std::filesystem::path runtime_dir() {
	if (g_runtime_dir) return *g_runtime_dir;
	return detent_agent::default_runtime_dir(g_system);
}

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
	if (env) if (const char* value = std::getenv(env); value && *value) return value;
	if (auto it = g_config.find(flag); it != g_config.end()) return it->second;
	return std::nullopt;
}

#ifndef DETENT_READER
bool prepared_reader(const std::vector<std::string> &args) {
	const auto validation = setting(args, "--reader-validation", nullptr).value_or("prepared");
	if (validation != "prepared" && validation != "full")
		throw std::invalid_argument("reader validation must be prepared or full");
	return validation == "prepared";
}
#endif

std::optional<pid_t> locked_pid() { return detent_cli::locked_pid(pid_path()); }

#ifndef DETENT_READER
int run_agent(const std::vector<std::string> &args) {
	const bool detach = has_flag(args, "--daemonize");
	if (g_system && detach) throw std::runtime_error("system services must run in the foreground");
	if (!g_managed_config)
		throw std::runtime_error("no source configured; pass --managed PRIVATE_CONFIG or set managed_path");
#ifdef DETENT_WITH_MANAGED_AGENT
	return detent_cli::run_daemon(
		detach, runtime_dir() / "agent.log", [&](const detent_cli::Ready &ready) {
			detent_cli::ProcessLock lock(pid_path(), g_system);
			if (!lock.acquired())
				return 0;
			detent_logging::Session logging(g_logging_settings);
			return detent_agent::run_managed(
				*g_managed_config, *g_generation_dir,
				has_flag(args, "--initialize-managed") ? detent::local::StoreOpenMode::FreshEmpty
													   : detent::local::StoreOpenMode::ExistingManaged,
				g_system, ready);
		}, detent_agent::managed_startup_timeout);
#else
	throw std::runtime_error("managed agent support is disabled in this build");
#endif
}

int cmd_status(const std::vector<std::string> &args) {
	auto pid = locked_pid();
	if (pid)
		std::printf("running (pid %d)\n", *pid);
	else
		std::printf("not running\n");
	if (g_generation_dir) {
		try {
			const bool prepared = prepared_reader(args);
			detent_agent::ReaderCapacity capacity([&](const auto &flag, const char *env) { return setting(args, flag, env); }, prepared);
			for (const auto &[name, descriptor] : detent::local::read_generation_selections(*g_generation_dir)) {
				detent::local::GenerationReader reader(*g_generation_dir, capacity.limits(), std::nullopt,
					prepared ? std::optional(detent_agent::publisher_uid(setting(args, "--publisher-user", nullptr), g_system)) : std::nullopt,
					name);
				reader.refresh();
				auto view = reader.read_view();
				std::printf("durable revision=%llu knob_count=%u namespace=%s\n",
							(unsigned long long)view.cursor().position, view.size(), name.c_str());
			}
		} catch (const std::exception &error) {
			std::printf("local generation unavailable: %s\n", error.what());
		}
	}
	return pid ? 0 : 1;
}

#endif

#ifdef DETENT_READER
void validate_get_args(const std::vector<std::string> &args) {
	if (args.empty() || args.front().starts_with('-'))
		throw std::runtime_error("get requires a knob name");
	constexpr std::string_view values[]{"--hash",
										"--switch",
										"--default",
										"--config",
										"--runtime-dir",
										"--managed",
										"--generation-dir",
										"--publisher-user",
										"--reader-validation"};
	for (std::size_t i = 1; i < args.size(); ++i) {
		const auto &arg = args[i];
		if (arg == "--system")
			continue;
		if (detent_agent::is_reader_option(arg) && arg != "--reader-mapping-bytes")
			throw std::runtime_error(arg + " is detent-agent --status's; detent get caps only its mapped bytes "
										   "(--reader-mapping-bytes)");
		if (arg != "--reader-mapping-bytes" && std::find(std::begin(values), std::end(values), arg) == std::end(values))
			throw std::runtime_error("unknown argument: " + arg);
		if (++i == args.size() || args[i].starts_with("--"))
			throw std::runtime_error(arg + " requires a value");
		if (arg == "--reader-validation" && args[i] != "prepared")
			throw std::runtime_error("detent get reads prepared generations only; --reader-validation " + args[i] +
									 " is detent-agent --status's");
	}
}

void launch_agent(const std::vector<std::string> &args) {
	std::filesystem::path executable(g_executable);
	auto daemon = executable.has_parent_path()
					  ? (std::filesystem::absolute(executable).parent_path() / "detent-agent").string()
					  : std::string("detent-agent");
	std::vector<std::string> start_args{daemon, "--daemonize", "--quiet"};
	for (std::size_t i = 1; i < args.size(); ++i) {
		if (args[i] == "--hash" || args[i] == "--switch" || args[i] == "--default") {
			++i;
			continue;
		}
		start_args.push_back(args[i]);
	}
	std::vector<char *> argv;
	for (auto &arg : start_args)
		argv.push_back(arg.data());
	argv.push_back(nullptr);
	auto child = ::fork();
	if (child < 0)
		throw std::runtime_error("cannot launch detent-agent");
	if (child == 0) {
		::execvp(daemon.c_str(), argv.data());
		std::fprintf(stderr, "detent: cannot execute %s: %s\n", daemon.c_str(), std::strerror(errno));
		_exit(127);
	}
	int status = 0;
	while (::waitpid(child, &status, 0) < 0) {
		if (errno != EINTR)
			throw std::runtime_error("cannot wait for detent-agent");
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		throw std::runtime_error("detent-agent startup failed");
}

// The byte cap on the reader's mapped packs and bitmaps: --reader-mapping-bytes, its variable or
// reader_mapping_bytes in the config; 0 (unset) is the core's 512 MiB.
std::uint64_t mapping_bytes(const std::vector<std::string> &args) {
	const auto text = setting(args, "--reader-mapping-bytes", "DETENT_READER_MAPPING_BYTES");
	if (!text)
		return 0;
	std::uint64_t value = 0;
	const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
	if (error != std::errc{} || end != text->data() + text->size() || !value)
		throw std::invalid_argument("invalid reader capacity: mapping-bytes");
	return value;
}

// Reads one knob through the reader core (D148) and prints its integer; a flag prints 0 or 1. When no
// generation is present and `--managed` names an agent, a user read starts it and waits for the first
// one. A failed lookup is an error, or the per-read `--default`, unless the store failed verification or
// trust.
int cmd_get(const std::vector<std::string> &args) {
	if (!g_generation_dir)
		throw std::runtime_error("no source configured; pass --managed PRIVATE_CONFIG or --generation-dir PATH");
	const auto fallback = arg_value(args, "--default");
	const auto &name = args[0];
	const auto colon = name.find(':');
	std::vector<std::string_view> namespaces;
	if (colon != std::string::npos)
		namespaces.push_back(std::string_view(name).substr(0, colon));
	const detent::Reader reader(*g_generation_dir,
								detent_agent::publisher_uid(setting(args, "--publisher-user", nullptr), g_system),
								namespaces, mapping_bytes(args));
	const auto hash = arg_value(args, "--hash"), switch_value = arg_value(args, "--switch");
	auto read = [&] {
		auto view = [](const std::optional<std::string> &text) -> std::optional<std::string_view> {
			if (text) return std::string_view(*text);
			return std::nullopt;
		};
		return reader.lookup(name, 0, view(hash), view(switch_value));
	};
	auto found = read();
	// A durable selected generation remains useful with the daemon stopped.
	if (found.status == DETENT_UNAVAILABLE && !g_system && g_managed_config) {
		try {
			launch_agent(args);
			const auto deadline = std::chrono::steady_clock::now() + detent_agent::managed_startup_timeout;
			while ((found = (reader.refresh(), read())).status == DETENT_UNAVAILABLE &&
				   std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(20ms);
		} catch (const std::exception &error) {
			if (!fallback)
				std::fprintf(stderr, "detent: managed agent startup: %s\n", error.what());
		}
	}
	if (found) {
		std::printf("%lld\n", static_cast<long long>(found.value));
		return 0;
	}
	if (fallback && found.status != DETENT_SOURCE_FAILURE && found.status != DETENT_UNTRUSTED) {
		std::printf("%s\n", fallback->c_str());
		return 0;
	}
	std::fprintf(stderr, "ERR %s: %s%s\n", detent_status_name(found.status), name.c_str(),
				 found.status == DETENT_UNAVAILABLE ? " (no durable local generation available)" : "");
	return 1;
}

#endif

#ifdef DETENT_READER
const char *kUsage = "usage: detent get NAME [--hash VAL] [--switch VAL] [--default VAL]\n"
					 "                  [--config PATH] [--runtime-dir PATH] [--system]\n"
					 "                  [--generation-dir PATH] [--managed PRIVATE_CONFIG]\n"
					 "       detent help | --help\n"
					 "A user read starts the managed detent-agent when no durable generation is present;\n"
					 "system reads attach to the managed service. Without --managed or --generation-dir\n"
					 "(or managed_path or generation_dir in the config) the read fails: no source configured.\n"
					 "Config: agent.json in the standard Detent config directory.\n"
					 "Managed readers: --publisher-user USER_OR_UID; --reader-mapping-bytes N caps the\n"
					 "bytes the reader maps (default 512 MiB). The other reader_* settings are detent-agent's.\n"
					 "Flags override environment variables, which override config.\n";
#else
const char *kUsage =
	"Logging: --log-level LEVEL, -v/-vv, --verbosity 0..2, --color auto|always|never|truecolor|256|16\n"
	"detent-agent: host-local managed configuration agent\n"
	"\n"
	"usage:\n"
	"  detent-agent --managed PRIVATE_CONFIG [--daemonize] [--generation-dir PATH]\n"
	"               [--initialize-managed]\n"
	"  detent-agent --status\n"
	"  detent-agent --help | -h\n"
	"\n"
	"Without --managed (or managed_path in the config) it fails: no source configured.\n"
	"Runs in the foreground unless --daemonize is supplied.\n"
	"Use signals or the service manager to stop/restart.\n"
	"Use detent get NAME to read or auto-start a user agent.\n"
	"\n"
	"Configuration: --config PATH, else $XDG_CONFIG_HOME/detent/agent.json\n"
	"(Linux/FreeBSD: ~/.config/detent; macOS: ~/Library/Application Support/Detent).\n"
	"System fallback: /etc/detent (FreeBSD: /usr/local/etc/detent; macOS: /Library/Application "
	"Support/Detent).\n"
	"JSON fields:\n"
	"runtime_dir, managed_path, generation_dir, publisher_user.\n"
	"managed_path and generation_dir select durable authority delivery.\n"
	"Paths in config are relative to the config file.\n"
	"Flags override environment variables, which override config settings.\n"
	"reader_* settings configure status reads; see USAGE.md for limits and trust policy.\n"
	"Managed readers: --publisher-user USER_OR_UID, --reader-validation prepared|full.\n"
	"--runtime-dir PATH selects the runtime directory for any command.\n"
	"--system uses system config and /run/detent (BSD/macOS: /var/run/detent).\n"
	"System agents run in the foreground; get does not start them.\n"
	"environment variables (optional):\n"
	"  DETENT_RUNTIME_DIR       pid/log directory\n"
	"                           (default $XDG_RUNTIME_DIR/detent, else $TMPDIR/detent-<uid>)\n"
	"  DETENT_MANAGED_CONFIG    private managed configuration\n"
	"  DETENT_GENERATION_DIR    durable generation directory\n";

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
#ifdef DETENT_READER
		if (command != "get")
			throw std::runtime_error("unknown command: " + command);
		if (args.empty()) {
			std::fputs(kUsage, stderr);
			return 2;
		}
		validate_get_args(args);
#endif
		g_system = has_flag(args, "--system");
		g_config = detent_agent::load_config(args);
#ifndef DETENT_READER
		g_logging_settings = detent_logging::parse(args, g_config);
		for (std::size_t i = 0; i < args.size(); ++i) {
			const auto& arg = args[i];
			if (arg == "--system" || arg == "--daemonize" || arg == "--status" || arg == "--quiet" ||
				arg == "--initialize-managed")
				continue;
			if (detent_agent::is_reader_option(arg) || arg == "--runtime-dir" ||
				arg == "--config" || arg == "--managed" || arg == "--generation-dir" ||
				arg == "--publisher-user" || arg == "--reader-validation") {
				if (++i == args.size() || args[i].starts_with('-')) throw std::runtime_error(arg + " requires a value");
				continue;
			}
			throw std::runtime_error("unknown argument: " + arg);
		}
#endif
		g_runtime_dir = setting(args, "--runtime-dir", "DETENT_RUNTIME_DIR");
		g_managed_config = setting(args, "--managed", "DETENT_MANAGED_CONFIG");
		g_generation_dir = setting(args, "--generation-dir", "DETENT_GENERATION_DIR");
		if (g_managed_config && !g_generation_dir)
			g_generation_dir = detent_cli::default_agent_state_dir(g_system).string();
#ifndef DETENT_READER
		if (has_flag(args, "--status")) {
			if (has_flag(args, "--daemonize")) throw std::runtime_error("--status and --daemonize are mutually exclusive");
			return cmd_status(args);
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
