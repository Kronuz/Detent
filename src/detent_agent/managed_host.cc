#include "managed_host.hh"
#include "config.hh"
#include "managed_host_internal.hh"
#include "../detent_cli/logging.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_local/directory.hh"
#include "detent_transport/await_io.hh"
#include <asio.hpp>
#include <atomic>
#include <cassert>
#include <functional>
#include <future>
#include <lifecycle.hh>
#include <optional>
#include <sys/stat.h>
#include <sysexits.h>

namespace detent_agent {
namespace d = detent::distribution;
namespace t = detent::delivery;
namespace l = detent::local;
namespace detail {
// Ready when every followed namespace is published or the source has answered for it (C4).
bool may_announce_ready(const t::CoordinatorStatus &state) noexcept {
	return state.initialized && state.ready && state.phase != t::CoordinatorPhase::Fenced &&
		   state.phase != t::CoordinatorPhase::Closing && state.phase != t::CoordinatorPhase::Closed;
}
task::task<void> monitor(task::asio_scheduler &scheduler, std::shared_ptr<t::Coordinator> coordinator,
						 detent_cli::Ready ready, std::chrono::milliseconds preparation_timeout) {
	asio::steady_timer timer(scheduler.executor());
	bool announced = false;
	bool timed_out = false;
	const auto preparation_deadline = std::chrono::steady_clock::now() + preparation_timeout;
	std::uint64_t reported = 0;
	std::uint64_t failures = 0, rotations = 0, refresh_failures = 0, damaged_packs = 0;
	l::SyncCounters published_sync;
	t::TransferStatus transfers;
	std::optional<std::chrono::steady_clock::time_point> quorum_open;
	while (!coordinator->drained()) {
		const auto state = coordinator->status();
		if (!announced && !timed_out && std::chrono::steady_clock::now() >= preparation_deadline) {
			timed_out = true;
			coordinator->close();
		}
		if (!announced && !timed_out && may_announce_ready(state)) {
			L_INFO("detent-agent: managed local store ready, {} namespaces selected",
				   state.selected.namespaces.size());
			try {
				ready();
			} catch (...) {
				coordinator->close();
				throw;
			}
			announced = true;
		}
		if (state.responses != reported) {
			L_DEBUG("detent-agent: managed publication of response {}, {} namespaces selected", state.responses,
					state.selected.namespaces.size());
			reported = state.responses;
		}
		if (state.source_rotations != rotations) {
			L_INFO("detent-agent: managed source endpoint {} after {} rotations", state.source_endpoint_index,
				   state.source_rotations);
			rotations = state.source_rotations;
		}
		if (state.quorum_refresh_failures != refresh_failures) {
			L_WARNING("detent-agent: managed quorum refresh failures {}", state.quorum_refresh_failures);
			refresh_failures = state.quorum_refresh_failures;
		}
		if (!(state.sync.publication == published_sync)) {
			published_sync = state.sync.publication;
			L_INFO("detent-agent: publication synced {} files in {} groups, {} directory syncs, {} device flushes; "
				   "{} device flushes since start (startup {})",
				   published_sync.files, published_sync.groups, published_sync.directory_syncs,
				   published_sync.device_flushes, state.sync.total.device_flushes,
				   state.sync.startup.device_flushes);
			L_INFO("detent-agent: hashed {} bytes since start: {} to build, {} to check, {} in frames",
				   state.hash.total_bytes(), state.hash.built_bytes(), state.hash.checked_bytes(),
				   state.hash.frame.bytes);
		}
		if (state.transfers.received != transfers.received || state.transfers.refused != transfers.refused) {
			transfers = state.transfers;
			L_INFO("detent-agent: page transfers {} received ({} bytes), {} refused", transfers.received,
				   transfers.bytes, transfers.refused);
		}
		if (state.scrub.damaged_packs != damaged_packs) {
			L_WARNING("detent-agent: managed scrub found {} damaged packs in the selected generation after {} "
					  "passes; reads of their pages fail with a source failure",
					  state.scrub.damaged_packs, state.scrub.passes);
			damaged_packs = state.scrub.damaged_packs;
		}
		if (state.last_quorum_open != quorum_open) {
			L_DEBUG("detent-agent: managed quorum Open succeeded at endpoint {}",
					state.source_endpoint_index);
			quorum_open = state.last_quorum_open;
		}
		if (state.failures != failures) {
			std::string detail;
			if (state.error) {
				try {
					std::rethrow_exception(state.error);
				} catch (const std::exception &error) {
					detail = error.what();
				} catch (...) {
					detail = "unknown failure";
				}
			}
			L_WARNING("detent-agent: managed delivery failures {}, preserving durable selection: {}",
					  state.failures, detail);
			failures = state.failures;
		}
		timer.expires_after(std::chrono::milliseconds(20));
		co_await detent::transport::await_timer(timer);
	}
	if (timed_out)
		throw std::runtime_error("managed preparation timed out after draining accepted work");
	const auto terminal = coordinator->status();
	if (terminal.phase == t::CoordinatorPhase::Fenced) {
		if (terminal.error)
			std::rethrow_exception(terminal.error);
		throw std::runtime_error("managed delivery fenced after drainage");
	}
	if (!announced && terminal.error)
		std::rethrow_exception(terminal.error);
	if (!announced)
		throw std::runtime_error("managed local startup failed before readiness");
}
} // namespace detail
namespace {
constexpr std::chrono::milliseconds kShutdownGrace{5'000};
// The secrets file is private (D125): a regular file, not a link, that no group or other can access.
void require_private_secrets(const std::filesystem::path &path) {
	struct stat status{};
	if (::lstat(path.c_str(), &status) != 0)
		throw std::runtime_error("cannot open managed secrets " + path.string());
	if (!S_ISREG(status.st_mode) || (status.st_mode & 0077))
		throw std::runtime_error("managed secrets " + path.string() + " must be a regular file with no group or other access");
}
// The coordinator as a component. start() runs it; stop() closes it, so it refuses new work and drains;
// drain() ends when it has, or at the stop bound's abandon.
struct CoordinatorComponent {
	std::shared_ptr<t::Coordinator> coordinator;
	std::function<void()> launch;
	task::event &done;
	std::atomic<bool> &finished;
	task::task<void> start(task::cancel_token) {
		launch();
		co_return;
	}
	void stop() { coordinator->close(); }
	task::task<std::vector<std::string>> drain(const task::stop_bound &bound) {
		task::cancel_callback abandoned(bound.abandon, [this] { done.set(); });
		co_await done;
		if (!finished.load(std::memory_order_acquire))
			co_return std::vector<std::string>{"managed delivery: still draining"};
		co_return std::vector<std::string>{};
	}
	void flush() {}
	std::string name() const { return "coordinator"; }
};
// Ready once the monitor announces it: start() waits for that, or for the stop.
struct ReadyComponent {
	task::event &announced;
	task::task<void> start(task::cancel_token stop) {
		task::cancel_callback ended(stop, [this] { announced.set(); });
		co_await announced;
	}
	void stop() {}
	task::task<std::vector<std::string>> drain(const task::stop_bound &) { co_return std::vector<std::string>{}; }
	void flush() {}
	std::string name() const { return "ready"; }
};
task::task<void> supervise(task::asio_scheduler &scheduler, std::shared_ptr<t::Coordinator> coordinator,
						   detent_cli::Ready ready) {
	co_await task::when_all(coordinator->run(), detail::monitor(scheduler, coordinator, std::move(ready),
																managed_startup_timeout));
}
} // namespace
static int run_managed_owned(const std::filesystem::path &configuration, const std::filesystem::path &directory,
				detent::local::StoreOpenMode mode, bool system, const ManagedSettings &settings,
				const detent_cli::Ready &ready, task::stop_signals &signals) {
	asio::io_context io(1);
	task::asio_scheduler scheduler(io.get_executor());
	asio::thread_pool worker(1);
	// Configuration, TLS files, entropy, census and semantic startup recovery
	// belong on the configuration worker, before owner admission starts.
	std::promise<ManagedOptions> configured;
	auto prepared = configured.get_future();
	asio::post(worker, [&] {
		try {
			require_private_secrets(configuration);
			configured.set_value(load_managed_options(configuration, directory, mode, system));
		} catch (...) {
			configured.set_exception(std::current_exception());
		}
	});
	auto options = prepared.get();
	auto account = std::make_shared<d::StorageLedger>(options.heap_bytes);
	d::RetirementRuntime runtime(account, 2);
	auto domain = runtime.domain(account, 4096);
	options.coordinator.view_limits.retirement_domain = &domain;
	options.coordinator.view_limits.mapping_account =
		std::make_shared<d::StorageLedger>(options.mapping_bytes);
	options.disk.recovery_limits = options.coordinator.view_limits;
	auto queue = kronuz::io::completion::make_native_queue(
		kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
	auto driver = std::allocate_shared<t::NativeDriver>(d::StorageAllocator<t::NativeDriver>(account),
														scheduler, queue, true);
	options.coordinator.source.scheduler = &scheduler;
	auto coordinator = t::prepare_coordinator(std::move(options.coordinator), driver, domain, worker);
	std::promise<std::shared_ptr<l::DiskStore>> opened;
	auto opening = opened.get_future();
	asio::post(worker, [&] {
		try {

			std::optional<l::ProvisionedDirectory> provisioned;
			if (mode == l::StoreOpenMode::FreshEmpty)
				provisioned.emplace(l::provision_directory(directory, system ? 0755 : 0700));
			auto store = l::make_disk_store(domain, directory, account, options.disk);
			if (provisioned) {
				provisioned->verify_name();
				if (provisioned->identity() != store->directory_identity())
					throw std::runtime_error("managed store differs from provisioned root");
			}
			opened.set_value(std::move(store));
		} catch (...) {
			opened.set_exception(std::current_exception());
		}
	});
	auto store = opening.get();
	coordinator->attach_store(store);
	task::event announced, done;
	std::atomic<bool> finished{false};
	CoordinatorComponent coordinator_component{coordinator, [&] {
		task::spawn(task::scheduler_ref::of(scheduler),
					supervise(scheduler, coordinator, [&] { announced.set(); }),
					[&](std::exception_ptr error) {
						if (error)
							signals.fail(error);
						finished.store(true, std::memory_order_release);
						done.set();
					});
	}, done, finished};
	ReadyComponent ready_component{announced};
	const task::stop_timing timing{settings.shutdown_timeout,
								   std::min<std::chrono::nanoseconds>(kShutdownGrace, settings.shutdown_timeout)};
	task::stopped result;
	task::spawn(task::scheduler_ref::of(scheduler),
				task::lifecycle(scheduler, signals, timing, ready, coordinator_component, ready_component),
				[&](std::exception_ptr error, std::optional<task::stopped> stopped) {
					result = stopped ? std::move(*stopped) : task::stopped{error};
					io.stop();
				});
	io.run(); // the lifecycle holds the loop's work guard until every flush returned
	// Teardown (D15): the loop has stopped, so close the scheduler, then destroy what is still suspended on it.
	scheduler.close();
	scheduler.destroy_roots();
	worker.join();
	coordinator.reset();
	driver.reset();
	queue.reset();
	store.reset();
	domain.close().wait();
	for (const auto &line : result.unfinished)
		L_WARNING("detent-agent: unfinished: {}", line);
	if (result.failure)
		std::rethrow_exception(result.failure);
	if (!result.ready && settings.detach)
		throw std::runtime_error("stopped before startup completed");
	L_NOTICE("detent-agent: managed delivery drained");
	return result.unfinished.empty() ? 0 : EX_TEMPFAIL;
}
int run_managed(const std::filesystem::path &configuration, const std::filesystem::path &directory,
				detent::local::StoreOpenMode mode, bool system, const ManagedSettings &settings,
				const detent_cli::Ready &ready, task::stop_signals &signals) {
	// The io_context may hold a cancelled deadline's handler until it is destroyed, so the check
	// follows the owned run (T13).
	[[maybe_unused]] const auto frames = task::detail::live_frames();
	const int result = run_managed_owned(configuration, directory, mode, system, settings, ready, signals);
	assert(task::detail::live_frames() == frames && "a task frame outlived the managed host (T13)");
	return result;
}
} // namespace detent_agent
