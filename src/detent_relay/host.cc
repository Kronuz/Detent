#include "host.hh"
#include "../detent_cli/logging.hh"
#include "detent/owner_loop.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_relay/configuration.hh"
#include "detent_relay/http_upstream.hh"
#include "detent_relay/service.hh"
#include <asio.hpp>
#include <cassert>
#include <charconv>
#include <atomic>
#include <future>
#include <http_asio.h>
#include <http_asio_tls.h>
#include <lifecycle.hh>
#include <sysexits.h>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::relay {
namespace d = distribution;
namespace {
constexpr std::chrono::milliseconds kShutdownGrace{5'000};
template <class Result, class Function> Result on_worker(asio::thread_pool &worker, Function function) {
	std::promise<Result> result;
	auto ready = result.get_future();
	asio::post(worker, [&] {
		try {
			result.set_value(function());
		} catch (...) {
			result.set_exception(std::current_exception());
		}
	});
	return ready.get();
}
std::shared_ptr<const d::OwnedBuffer> health_snapshot(std::shared_ptr<d::StorageLedger> account,
													const d::StorageLedger &root, ServiceStatus service,
													const MirrorService &mirror, bool ready, std::uint64_t turns) {
	auto output = d::make_owned_buffer(std::move(account), 4096);
	auto number = [&](std::uint64_t value) {
		std::array<char, 32> bytes;
		const auto converted = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
		output->append({bytes.data(), static_cast<std::size_t>(converted.ptr - bytes.data())});
	};
	auto field = [&](std::string_view name, std::uint64_t value) {
		output->append(name);
		number(value);
	};
	output->append(ready ? "{\"ready\":true" : "{\"ready\":false");
	field(",\"turns\":", turns);
	field(",\"heap_used\":", root.used());
	field(",\"heap_peak\":", root.peak());
	field(",\"heap_limit\":", root.limit());
	field(",\"opens\":", service.requests[0]);
	field(",\"polls\":", service.requests[1]);
	field(",\"closes\":", service.requests[2]);
	const auto state = mirror.status();
	field(",\"mirror\":{\"ready\":", state.ready);
	field(",\"failed\":", state.failed);
	field(",\"syncs\":", state.syncs);
	field(",\"sync_failures\":", state.sync_failures);
	field(",\"entries\":", state.entries);
	field(",\"fetched_files\":", state.fetched_files);
	field(",\"fetched_bytes\":", state.fetched_bytes);
	field(",\"reused_files\":", state.reused_files);
	field(",\"collected_files\":", state.collected_files);
	field(",\"answers\":", state.answers);
	field(",\"idle\":", state.idle);
	field(",\"sessions\":", state.sessions);
	field(",\"failed_namespaces\":", state.failed_namespaces);
	field(",\"pages\":{\"builds\":", state.pages.builds);
	field(",\"incremental\":", state.pages.incremental);
	field(",\"relocations\":", state.pages.relocations);
	field(",\"build_failures\":", state.pages.build_failures);
	field(",\"full_copies\":", state.pages.full_copies);
	field(",\"deltas\":", state.pages.deltas);
	field(",\"delta_records\":", state.pages.delta_records);
	output->append("}}}");
	output->seal_prefix();
	return output;
}
// The relay's state on its owner loop. stop() closes the runtime and the mirror on the owner thread, so
// every owner wait ends with its truthful reply; drain() runs after the listener's, tells the owner no
// request can arrive, and ends once the owner's turn found every request drained, or at the stop bound's
// abandon.
struct StateComponent {
	OwnerLoop &owner;
	std::function<void()> shutdown, listener_done;
	task::event &drained;
	std::atomic<bool> &finished;
	task::task<void> start(task::cancel_token) { co_return; }
	void stop() { asio::post(owner.io(), shutdown); }
	task::task<std::vector<std::string>> drain(const task::stop_bound &bound) {
		asio::post(owner.io(), listener_done);
		task::cancel_callback abandoned(bound.abandon, [this] { drained.set(); });
		co_await drained;
		if (!finished.load(std::memory_order_acquire))
			co_return std::vector<std::string>{"relay: requests still in flight"};
		co_return std::vector<std::string>{};
	}
	void flush() {}
	std::string name() const { return "relay"; }
};
// The http listener: start() binds; drain() runs http's stop, which cancels the requests in flight (their
// responses are still written), gives them the grace, then forces.
struct ListenerComponent {
	http::HttpAsioService &listener;
	unsigned short port;
	task::task<void> start(task::cancel_token) {
		listener.start(port);
		co_return;
	}
	void stop() {}
	task::task<std::vector<std::string>> drain(const task::stop_bound &) {
		listener.stop();
		co_return std::vector<std::string>{};
	}
	void flush() {}
	std::string name() const { return "http"; }
};
// Ready once the mirror is: start() waits for the owner's turn to announce it, or for the stop.
struct ReadyComponent {
	task::event &mirror_ready;
	task::task<void> start(task::cancel_token stop) {
		task::cancel_callback ended(stop, [this] { mirror_ready.set(); });
		co_await mirror_ready;
	}
	void stop() {}
	task::task<std::vector<std::string>> drain(const task::stop_bound &) { co_return std::vector<std::string>{}; }
	void flush() {}
	std::string name() const { return "mirror"; }
};
} // namespace
static int run_host_owned(const HostSettings &settings, const detent_cli::Ready &ready,
						  task::stop_signals &signals) {
	const auto &configuration = settings.configuration;
	OwnerLoop owner;
	auto &wake = owner.wake();
	asio::thread_pool startup(1);
	auto options = on_worker<HostOptions>(startup, [&] { return load_host_options(configuration); });
	auto root = std::make_shared<d::StorageLedger>(options.heap_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(root, options.baseline_bytes);
	auto control = std::allocate_shared<d::StorageLedger>(d::StorageAllocator<d::StorageLedger>(baseline),
														  root, options.control_bytes);
	auto child = [&](std::shared_ptr<d::StorageLedger> parent, std::size_t bytes) {
		return std::allocate_shared<d::StorageLedger>(d::StorageAllocator<d::StorageLedger>(baseline),
													  std::move(parent), bytes);
	};
	const auto open_bytes = options.control_bytes / 8;
	const auto poll_bytes = options.control_bytes * 3 / 8;
	const auto close_bytes = options.control_bytes / 32;
	ServiceAccounts accounts{
		child(control, options.control_bytes - open_bytes - poll_bytes - close_bytes),
		{child(control, open_bytes), child(control, poll_bytes), child(control, close_bytes)}};
	// W8: the namespace mirror, a reader of the authority or an upstream relay, never a member.
	std::shared_ptr<MirrorService> mirror;
	{
		auto &configured = options.mirror;
		on_worker<bool>(startup, [&] {
			const auto &directory = configured.service.directory;
			if (!std::filesystem::exists(directory)) {
				std::filesystem::create_directory(directory);
				std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			}
			struct stat info{};
			if (::stat(directory.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != ::geteuid() ||
				(info.st_mode & 0077))
				throw std::invalid_argument("relay mirror directory requires service ownership and mode 0700");
			return true;
		});
		mirror = std::make_shared<MirrorService>(configured.service,
												 std::make_unique<HttpUpstream>(configured.upstream),
												 child(root, configured.answer_bytes));
	}
	auto registry =
		std::allocate_shared<Registry>(d::StorageAllocator<Registry>(baseline), baseline, options.credentials);
	auto runtime = std::allocate_shared<ServiceRuntime>(d::StorageAllocator<ServiceRuntime>(baseline), accounts,
														registry, mirror, options.service_limits, [wake] { wake.raise(); });
	if (settings.max_clients)
		options.connections = *settings.max_clients;
	auto facade = std::make_unique<Service>(runtime);
	http::HttpAsioService listener(*facade, settings.reactors, settings.max_in_flight.value_or(options.connections),
								   http::tls_connection_factory(options.tls_context), options.connections);
	http::AsioBindOptions bind;
	bind.address = options.endpoint.address().to_string();
	listener.set_bind_options(bind);
	listener.set_request_timeout(std::chrono::seconds(65));
	listener.set_idle_timeout(settings.idle_timeout);
	listener.set_shutdown_grace(std::min(kShutdownGrace, settings.shutdown_timeout));
	task::event owner_drained, mirror_ready;
	std::atomic<bool> owner_finished{false};
	bool stop_requested = false, listener_stopped = false, announced = false;
	auto shutdown = [&] {
		if (stop_requested)
			return;
		stop_requested = true;
		runtime->close();
		mirror->close();
		wake.raise();
	};
	// Each turn visits a share of the requests; it runs again at once while it changes anything, else after
	// 1 ms. Once a round over every request (from a wake, a change or a long sleep) changes nothing, it sleeps
	// on `tick` until the next health report or a wake: a request, a reply, a mirror answer, the stop.
	// Mirror readiness and failure are read at the next health turn, at most 100 ms later.
	asio::steady_timer tick(owner.io());
	std::uint64_t turns = 0;
	bool round = true; // the next turn starts a round
	std::chrono::steady_clock::time_point health_due{};
	std::string sync_error; // the last sync failure logged; cleared by a successful sync
	std::function<void()> turn;
	auto sleep = [&](std::chrono::steady_clock::time_point until) {
		wake.arm(tick, until);
		tick.async_wait([&](const asio::error_code &error) {
			if (!error || error == asio::error::operation_aborted)
				turn();
		});
	};
	turn = [&] {
		++turns;
		try {
			if (wake.begin_turn() || round)
				runtime->rescan();
			round = false;
			const auto now = std::chrono::steady_clock::now();
			const bool progress = runtime->step(now);
			const auto state = mirror->status();
			if (state.failed && !stop_requested) {
				L_WARNING("detent-relay: namespace mirror stopped: {}", state.error);
				signals.fail(std::make_exception_ptr(std::runtime_error("relay mirror: " + state.error)));
				shutdown();
			}
			if (!state.failed && state.error != sync_error) {
				if (!state.error.empty())
					L_WARNING("detent-relay: mirror sync failed: {}", state.error);
				sync_error = state.error;
			}
			if (!announced && !stop_requested && state.ready) {
				runtime->health(health_snapshot(accounts.metadata, *root, runtime->status(), *mirror, true, turns));
				health_due = now + std::chrono::milliseconds(100);
				announced = true;
				mirror_ready.set();
				L_INFO("detent-relay: the namespace mirror is ready on {}:{}", options.endpoint.address().to_string(),
					   listener.port());
			}
			if (now >= health_due) {
				try {
					runtime->health(health_snapshot(accounts.metadata, *root, runtime->status(), *mirror,
													announced && !stop_requested, turns));
				} catch (const d::StoragePressure &) {
				}
				health_due = now + std::chrono::milliseconds(100);
			}
			if (listener_stopped && runtime->drained()) {
				wake.release();
				owner.io().stop();
				owner_finished.store(true, std::memory_order_release);
				owner_drained.set();
				return;
			}
			if (progress) {
				round = true;
				asio::post(owner.io(), turn);
			} else if (runtime->swept()) {
				round = true;
				sleep(health_due);
			} else {
				sleep(now + std::chrono::milliseconds(1));
			}
		} catch (...) {
			signals.fail(std::current_exception());
			shutdown();
			// One retry at once (shutdown raised the wake); a turn that keeps failing retries every 100 ms.
			sleep(std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
		}
	};
	asio::post(owner.io(), turn);
	// The control loop runs on this thread; the owner keeps its own.
	asio::io_context control_io(1);
	task::asio_scheduler loop(control_io.get_executor());
	StateComponent state{owner,
						 shutdown,
						 [&] {
							 listener_stopped = true;
							 wake.raise();
						 },
						 owner_drained,
						 owner_finished};
	ListenerComponent http_component{listener, options.endpoint.port()};
	ReadyComponent ready_component{mirror_ready};
	const task::stop_timing timing{settings.shutdown_timeout,
								   std::min<std::chrono::nanoseconds>(kShutdownGrace, settings.shutdown_timeout)};
	task::stopped result;
	task::spawn(task::scheduler_ref::of(loop),
				task::lifecycle(loop, signals, timing, ready, state, http_component, ready_component),
				[&](std::exception_ptr error, std::optional<task::stopped> stopped) {
					result = stopped ? std::move(*stopped) : task::stopped{error};
					control_io.stop();
				});
	control_io.run(); // the lifecycle holds the loop's work guard until every flush returned
	// Teardown (D15): the lifecycle stopped and drained the components; the owner stops (phase 1), every
	// scheduler closes (phase 2) and destroys its roots (phase 3) with every io_context alive, before
	// anything the tasks reference is destroyed.
	owner.stop();
	listener.close();
	owner.close();
	loop.close();
	listener.destroy_roots();
	owner.destroy_roots();
	loop.destroy_roots();
	startup.join();
	facade.reset();
	runtime.reset();
	mirror.reset();
	registry.reset();
	for (const auto &line : result.unfinished)
		L_WARNING("detent-relay: unfinished: {}", line);
	L_NOTICE("detent-relay: drained; admitted heap peak {} bytes, configured {}", root->peak(),
			 root->limit());
	if (result.failure)
		std::rethrow_exception(result.failure);
	if (!result.ready && settings.detach)
		throw std::runtime_error("stopped before startup completed");
	return result.unfinished.empty() ? 0 : EX_TEMPFAIL;
}
int run_host(const HostSettings &settings, const detent_cli::Ready &ready, task::stop_signals &signals) {
	// The io_context may hold a cancelled deadline's handler until it is destroyed, so the check
	// follows the owned run (T13).
	[[maybe_unused]] const auto frames = task::detail::live_frames();
	const int result = run_host_owned(settings, ready, signals);
	assert(task::detail::live_frames() == frames && "a task frame outlived the relay host (T13)");
	return result;
}
} // namespace detent::relay
