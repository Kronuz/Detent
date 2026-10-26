// Shared CLI entry point for detent-agent lifecycle and detent reads.
// DETENT_READER excludes daemon and HTTP code from the reader binary.
//
// Configuration: flags override environment variables, then agent.json.
// Baked defaults are optional; source-only agents start with an empty snapshot.
#ifndef DETENT_READER
#include "agent.hh"
#include "../detent_cli/logging.hh"
#include "detent_http/http_source.hh"
#ifdef DETENT_WITH_MANAGED_AGENT
#include "managed_host.hh"
#endif
#endif
#include "../detent_cli/daemon.hh"
#include "config.hh"
#include "reader_capacity.hh"
#include "detent/identity.hh"
#include "detent_http/address.hh"
#include "detent_local/generation.hh"
#include "detent_local/reader.hh"
#include "shm_reader.hh"
#ifdef DETENT_READER
#include "detent/client.hh"
#include "detent_local/client_source.hh"
#endif

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
std::optional<detent::distribution::Cursor> g_generation_identity;
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
	if (env) if (const char* value = std::getenv(env); value && *value) return value;
	if (auto it = g_config.find(flag); it != g_config.end()) return it->second;
	return std::nullopt;
}

bool prepared_reader(const std::vector<std::string> &args) {
	const auto validation = setting(args, "--reader-validation", nullptr).value_or("prepared");
	if (validation != "prepared" && validation != "full")
		throw std::invalid_argument("reader validation must be prepared or full");
	return validation == "prepared";
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
	if (has_flag(args, "--initialize-managed") && has_flag(args, "--upgrade-managed"))
		throw std::runtime_error("managed initialization and upgrade are mutually exclusive");
	if (g_managed_config) {
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
					: has_flag(args, "--upgrade-managed")  ? detent::local::StoreOpenMode::UpgradeLegacy
														   : detent::local::StoreOpenMode::ExistingManaged,
					g_system, ready);
			}, detent_agent::managed_startup_timeout);
#else
		throw std::runtime_error("managed agent support is disabled in this build");
#endif
	}
	if (has_flag(args, "--initialize-managed") || has_flag(args, "--upgrade-managed") || g_generation_dir)
		throw std::runtime_error("managed generation settings require --managed");
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

int cmd_status(const std::vector<std::string> &args) {
	auto pid = locked_pid();
	if (g_generation_dir) {
		if (pid)
			std::printf("running (pid %d)\n", *pid);
		else
			std::printf("not running\n");
		try {
			const bool prepared = prepared_reader(args);
			detent_agent::ReaderCapacity capacity([&](const auto &flag, const char *env) { return setting(args, flag, env); }, prepared);
			detent::local::GenerationReader reader(*g_generation_dir, capacity.limits(), g_generation_identity,
				prepared ? std::optional(detent_agent::publisher_uid(setting(args, "--publisher-user", nullptr), g_system)) : std::nullopt);
			reader.refresh();
			auto view = reader.read_view();
			std::printf("durable revision=%llu knob_count=%u\n", (unsigned long long)view.cursor().revision,
						view.size());
		} catch (const std::exception &error) {
			std::printf("local generation unavailable: %s\n", error.what());
		}
		return pid ? 0 : 1;
	}
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
void validate_get_args(const std::vector<std::string> &args) {
	if (args.empty() || args.front().starts_with('-'))
		throw std::runtime_error("get requires a knob name");
	constexpr std::string_view values[]{"--hash",
										"--switch",
										"--default",
										"--config",
										"--defaults",
										"--source-path",
										"--source-address",
										"--source-http",
										"--poll-interval-ms",
										"--runtime-dir",
										"--managed",
										"--generation-dir",
										"--publisher-user",
										"--reader-validation",
										"--generation-cluster",
										"--generation-group",
										"--generation-epoch",
										"--generation-scope"};
	for (std::size_t i = 1; i < args.size(); ++i) {
		const auto &arg = args[i];
		if (arg == "--system")
			continue;
		if (!detent_agent::is_reader_option(arg) && std::find(std::begin(values), std::end(values), arg) == std::end(values))
			throw std::runtime_error("unknown argument: " + arg);
		if (++i == args.size() || args[i].starts_with("--"))
			throw std::runtime_error(arg + " requires a value");
	}
}

// Detects the knob's type through bool evaluation before trying an int read.
template <class Reader>
std::optional<std::string>
try_read(const Reader &reader, const std::string &name, const std::optional<std::string> &hash_val,
		 const std::optional<std::string> &switch_val, detent::KnobError *out_error) {
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

int cmd_managed_get(const std::vector<std::string> &args) {
	const bool prepared = prepared_reader(args);
	detent_agent::ReaderCapacity capacity([&](const auto &flag, const char *env) { return setting(args, flag, env); }, prepared);
	auto fallback = arg_value(args, "--default");
	detent::InitOptions options;
	if (auto defaults = setting(args, "--defaults", "DETENT_DEFAULTS_PATH"))
		options.defaults_path = *defaults;
	detent::local::ClientSourceOptions local;
	local.directory = *g_generation_dir;
	local.limits = capacity.limits();
	local.auto_refresh = false;
	local.identity = g_generation_identity;
	if (prepared)
		local.prepared_publisher = detent_agent::publisher_uid(setting(args, "--publisher-user", nullptr), g_system);
	std::shared_ptr<detent::local::LocalReadSource> source;
	options.read_source = [&](std::shared_ptr<const detent::Snapshot> defaults,
							  std::shared_ptr<const detent::OverrideTable> overrides) {
		source = std::make_shared<detent::local::LocalReadSource>(local, std::move(defaults),
																  std::move(overrides));
		return source;
	};
	std::unique_ptr<detent::Client> client;
	bool available = false;
	bool validation_failed = false;
	const auto deadline = std::chrono::steady_clock::now() + detent_agent::managed_startup_timeout;
	auto attempt = [&] {
		try {
			if (!client) client = std::make_unique<detent::Client>(options);
			if (!prepared) source->refresh();
			available = true;
		} catch (const detent::local::GenerationUnavailable &) {
		} catch (const std::exception &) {
			if (prepared) { validation_failed = true; throw; }
		}
	};
	attempt();
	// A durable selected image remains useful with the daemon stopped.
	if (!available && !g_system && g_managed_config) {
		try {
			launch_agent(args);
			do {
				attempt();
				if (available)
					break;
				std::this_thread::sleep_for(20ms);
			} while (std::chrono::steady_clock::now() < deadline);
		} catch (const std::exception &error) {
			if (validation_failed) throw;
			if (!fallback)
				std::fprintf(stderr, "detent: managed agent startup: %s\n", error.what());
		}
	}
	if (!available) {
		detent::InitOptions baked;
		baked.defaults_path = options.defaults_path;
		client = std::make_unique<detent::Client>(std::move(baked));
	}
	auto view = client->read_view();
	detent::KnobError error{detent::ErrorCategory::Uninitialized, "", ""};
	auto result = try_read(view, args[0], arg_value(args, "--hash"), arg_value(args, "--switch"), &error);
	if (result) {
		std::printf("%s\n", result->c_str());
		return 0;
	}
	if (fallback && error.category != detent::ErrorCategory::SourceFailure) {
		std::printf("%s\n", fallback->c_str());
		return 0;
	}
	std::fprintf(stderr, "ERR %s%s\n", error.what().c_str(),
				 available ? "" : " (no durable local generation available)");
	return 1;
}

int cmd_get(const std::vector<std::string>& args) {
	if (g_generation_dir)
		return cmd_managed_get(args);
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
			launch_agent(args);
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
const char *kUsage = "usage: detent get NAME [--hash VAL] [--switch VAL] [--default VAL]\n"
					 "                  [--config PATH] [--defaults PATH] [--source-address HOST:PORT]\n"
					 "                  [--source-path PATH] [--runtime-dir PATH] [--system]\n"
					 "                  [--generation-dir PATH] [--managed PRIVATE_CONFIG]\n"
					 "       detent help | --help\n"
					 "User reads auto-start detent-agent; system reads attach to the managed service.\n"
					 "Config: agent.json in the standard Detent config directory.\n"
					 "Reader capacity: --reader-records N, --reader-image-bytes N, --reader-heap-bytes N,\n"
					 "--reader-mapping-bytes N; see USAGE.md for all reader_* settings.\n"
					 "Flags override environment variables, which override config.\n";
#else
const char *kUsage =
	"Logging: --log-level LEVEL, -v/-vv, --verbosity 0..2, --color auto|always|never|truecolor|256|16\n"
	"detent-agent: host-local managed or legacy configuration daemon\n"
	"\n"
	"usage:\n"
	"  detent-agent [--daemonize] [--defaults PATH] [--source-path PATH]\n"
	"                      [--source-address HOST:PORT] [--poll-interval-ms N]\n"
	"  detent-agent --managed PRIVATE_CONFIG [--generation-dir PATH]\n"
	"               [--initialize-managed | --upgrade-managed]\n"
	"  detent-agent --status\n"
	"  detent-agent --help | -h\n"
	"\n"
	"Runs in the foreground unless --daemonize is supplied.\n"
	"Use signals or the service manager to stop/restart.\n"
	"Use detent get NAME to read or auto-start a user agent.\n"
	"\n"
	"Configuration: --config PATH, else $XDG_CONFIG_HOME/detent/agent.json\n"
	"(Linux/FreeBSD: ~/.config/detent; macOS: ~/Library/Application Support/Detent).\n"
	"System fallback: /etc/detent (FreeBSD: /usr/local/etc/detent; macOS: /Library/Application "
	"Support/Detent).\n"
	"JSON fields:\n"
	"source_address, source_path, defaults_path, runtime_dir, poll_interval_ms.\n"
	"managed_path and generation_dir select durable authority delivery.\n"
	"--generation-cluster/--generation-group/--generation-epoch/--generation-scope\n"
	"bind reader identity; supply all four as lowercase hexadecimal values.\n"
	"Paths in config are relative to the config file. Defaults are optional;\n"
	"defaults.json beside agent.json is discovered automatically if present.\n"
	"--source-http is a legacy alias for --source-address (HTTP transport).\n"
	"Flags override environment variables, which override config settings.\n"
	"reader_* settings configure status reads; see USAGE.md for limits and trust policy.\n"
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
				arg == "--initialize-managed" || arg == "--upgrade-managed")
				continue;
			if (detent_agent::is_reader_option(arg) || arg == "--defaults" || arg == "--source-path" || arg == "--source-address" ||
				arg == "--source-http" || arg == "--poll-interval-ms" || arg == "--runtime-dir" ||
				arg == "--config" || arg == "--managed" || arg == "--generation-dir" ||
				arg == "--publisher-user" || arg == "--reader-validation" ||
				arg == "--generation-cluster" || arg == "--generation-group" || arg == "--generation-epoch" ||
				arg == "--generation-scope") {
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
		std::array<std::optional<std::string>, 4> identity;
		const std::array<const char *, 4> identity_flags{"--generation-cluster", "--generation-group",
														 "--generation-epoch", "--generation-scope"};
		for (std::size_t i = 0; i < identity.size(); ++i) {
			identity[i] = arg_value(args, identity_flags[i]);
			if (!identity[i]) {
				if (auto it = g_config.find(identity_flags[i]); it != g_config.end())
					identity[i] = it->second;
			}
		}
		if (std::any_of(identity.begin(), identity.end(),
						[](const auto &part) { return part.has_value(); })) {
			if (!g_generation_dir || !std::all_of(identity.begin(), identity.end(),
												  [](const auto &part) { return part.has_value(); }))
				throw std::runtime_error(
					"generation identity requires directory, cluster, group, epoch and scope");
			detent::distribution::Cursor expected;
			expected.cluster = detent::distribution::parse_identity_hex<16>(*identity[0]);
			expected.group = detent::distribution::parse_identity_hex<16>(*identity[1]);
			expected.restore_epoch = detent::distribution::parse_identity_hex<16>(*identity[2]);
			expected.scope = detent::distribution::parse_identity_hex<32>(*identity[3]);
			g_generation_identity = expected;
		}
		if (g_managed_config && (arg_value(args, "--source-path") || arg_value(args, "--source-address") ||
								 arg_value(args, "--source-http")))
			throw std::runtime_error("--managed and legacy source flags are mutually exclusive");
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
