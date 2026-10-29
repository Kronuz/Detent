#include "managed_host.hh"
#include "config.hh"
#include "managed_host_internal.hh"
#include "../detent_cli/logging.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_local/directory.hh"
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <functional>
#include <future>

namespace detent_agent {
namespace d = detent::distribution;
namespace t = detent::delivery;
namespace l = detent::local;
int verify_legacy_metadata(const std::filesystem::path &directory) {
	auto baseline = std::make_shared<d::StorageLedger>(1024 * 1024);
	auto account = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account);
	{
		auto session = l::make_owned_storage_session(domain, directory, account, false);
		l::verify_legacy_reader_metadata(directory, *session);
		session->close();
	}
	domain.close().wait();
	std::printf("legacy reader metadata verified; no capsule records remain\n");
	return 0;
}
namespace detail {
bool may_announce_ready(const t::CoordinatorStatus &state, bool require_prepared) noexcept {
	if (!state.initialized || state.phase == t::CoordinatorPhase::Fenced ||
		state.phase == t::CoordinatorPhase::Closing || state.phase == t::CoordinatorPhase::Closed)
		return false;
	if (state.selected.knowledge == l::SelectionKnowledge::Absent)
		return !state.selected.descriptor;
	return state.selected.knowledge == l::SelectionKnowledge::Known && state.selected.descriptor &&
		(!require_prepared || state.prepared_ready);
}
asio::awaitable<void> monitor(std::shared_ptr<t::Coordinator> coordinator, detent_cli::Ready ready,
							 bool require_prepared, std::chrono::milliseconds preparation_timeout) {
	asio::steady_timer timer(co_await asio::this_coro::executor);
	bool announced = false;
	bool timed_out = false;
	const auto preparation_deadline = std::chrono::steady_clock::now() + preparation_timeout;
	std::optional<d::Cursor> reported;
	std::uint64_t failures = 0, rotations = 0, refresh_failures = 0;
	std::optional<std::chrono::steady_clock::time_point> quorum_open;
	while (!coordinator->drained()) {
		const auto state = coordinator->status();
		if (!announced && !timed_out && std::chrono::steady_clock::now() >= preparation_deadline) {
			timed_out = true;
			coordinator->close();
		}
		if (!announced && !timed_out && may_announce_ready(state, require_prepared)) {
			if (state.selected.descriptor)
				L_INFO("detent-agent: managed local store ready, selected generation {}",
					   state.selected.descriptor->cursor.revision);
			else
				L_INFO("detent-agent: managed delivery running, awaiting its first generation");
			try {
				ready();
			} catch (...) {
				coordinator->close();
				throw;
			}
			announced = true;
		}
		if (state.acknowledged != reported) {
			L_DEBUG("detent-agent: managed publication acknowledged at revision {}",
					state.acknowledged ? state.acknowledged->revision : 0);
			reported = state.acknowledged;
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
		co_await timer.async_wait(asio::use_awaitable);
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
asio::awaitable<void> supervise(std::shared_ptr<t::Coordinator> coordinator, detent_cli::Ready ready,
							   bool require_prepared) {
	using namespace asio::experimental::awaitable_operators;
	co_await (coordinator->run() && detail::monitor(coordinator, std::move(ready), require_prepared,
		managed_startup_timeout));
}
} // namespace
int run_managed(const std::filesystem::path &configuration, const std::filesystem::path &directory,
				detent::local::StoreOpenMode mode, bool system, const detent_cli::Ready &ready) {
	asio::io_context io(1);
	asio::thread_pool worker(1);
	// Configuration, TLS files, entropy, census and semantic startup recovery
	// belong on the configuration worker, before owner admission starts.
	std::promise<ManagedOptions> configured;
	auto prepared = configured.get_future();
	asio::post(worker, [&] {
		try {
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
														io.get_executor(), queue, true);
	const bool require_prepared = options.coordinator.image_format == t::LocalImageFormat::PagedV2;
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
	asio::signal_set signals(io, SIGINT, SIGTERM);
	signals.async_wait([coordinator](const asio::error_code &error, int) {
		if (!error)
			coordinator->close();
	});
	auto done = asio::co_spawn(io, supervise(coordinator, ready, require_prepared), asio::use_future);
	// The signal wait must not retain an otherwise drained io_context forever.
	asio::steady_timer shutdown(io);
	std::function<void(const asio::error_code &)> watch;
	watch = [&](const asio::error_code &error) {
		if (error)
			return;
		if (coordinator->drained()) {
			signals.cancel();
			return;
		}
		shutdown.expires_after(std::chrono::milliseconds(20));
		shutdown.async_wait(watch);
	};
	shutdown.expires_after(std::chrono::milliseconds(20));
	shutdown.async_wait(watch);
	io.run();
	std::exception_ptr failure;
	try {
		done.get();
	} catch (...) {
		failure = std::current_exception();
	}
	worker.join();
	coordinator.reset();
	driver.reset();
	queue.reset();
	store.reset();
	domain.close().wait();
	if (failure)
		std::rethrow_exception(failure);
	L_NOTICE("detent-agent: managed delivery drained");
	return 0;
}
} // namespace detent_agent
