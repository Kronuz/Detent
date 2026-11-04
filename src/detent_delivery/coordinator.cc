#include "paged_limits.hh"
#include "paged_page_access.hh"
#include "detent_delivery/coordinator.hh"
#include "candidate_flat_validator.hh"
#include "coordinator_internal.hh"
#include "detent_delivery/reconnect_backoff.hh"
#include "managed_recovery.hh"
#include "pack_scrub.hh"
#include "paged_compaction_plan.hh"
#include "scrub_source.hh"
#include "paged_delta_plan.hh"
#include "paged_maintenance_plan.hh"
#include "paged_reuse.hh"
#include "paged_snapshot_plan.hh"
#include "prepared_reader.hh"
#include "source_selection.hh"
#include "state_equivalence.hh"
#include <algorithm>
#include <asio.hpp>
#include <openssl/rand.h>
#include <unistd.h>

namespace detent::delivery {
namespace d = distribution;
namespace l = local;
namespace {
bool fatal_source_reset(SessionResetReason reason) noexcept {
	return reason == SessionResetReason::Forbidden || reason == SessionResetReason::Unauthorized ||
		   reason == SessionResetReason::IdentityChanged || reason == SessionResetReason::InvalidRequest;
}
// The directory-bearing recovery result is destroyed on the cleanup worker.
// Builders may borrow it only while its retirement handle remains retained.
struct RecoveryResult : d::RetirementNode {
	explicit RecoveryResult(const CoordinatorOptions &options)
		: directory(options.directory), limits(options.view_limits) {}
	const std::filesystem::path directory;
	const l::ViewLimits limits;
	std::optional<l::RecoveredGeneration> generation;
	bool prepared = false;
	l::GenerationId next_id{};
	std::exception_ptr failure;
	std::atomic<bool> ready{false};
};
struct MaintenanceFailure {
	std::exception_ptr cause;
};
struct ConstructionControl {
	std::atomic<bool> stop{false};
	std::atomic<std::uint64_t> steps{0};
	void step() {
		if (stop.load(std::memory_order_acquire))
			throw asio::system_error(asio::error::operation_aborted);
		steps.fetch_add(1, std::memory_order_relaxed);
	}
};
struct PagedCandidateResult : d::RetirementNode {
	ConstructionControl control;
	bool compaction = false;
	std::optional<l::detail::PagedPlan> plan;
	std::optional<l::detail::StateEquivalenceProof> unchanged;
	std::shared_ptr<const l::detail::StateEquivalenceProof> maintenance;
	// A repair plan replaces the selected generation; in place when it keeps the selected cursor.
	bool repair = false, repair_in_place = false;
	std::optional<l::Descriptor> descriptor;
	std::optional<l::Descriptor> selected_base;
	std::exception_ptr failure;
	std::atomic<bool> ready{false};
};
struct CandidateComparisonResult : d::RetirementNode {
	ConstructionControl control;
	std::optional<l::detail::StateEquivalenceProof> proof;
	std::exception_ptr failure;
	std::atomic<bool> ready{false};
};
} // namespace
struct Coordinator::Impl {
	CoordinatorOptions options;
	detail::SourceSelection sources;
	std::shared_ptr<l::DiskStore> store;
	std::shared_ptr<NativeDriver> driver;
	d::RetirementDomain &domain;
	asio::thread_pool &worker;
	CoordinatorStatus diagnostic;
	std::exception_ptr terminal_failure;
	detail::CoordinatorAccess::ReconnectObservation reconnect_observation;
	ConstructionControl *construction = nullptr;
	std::uint64_t steps_at_stop = 0, settled_construction_steps = 0;
	bool construction_stopped = false;
	bool terminal_fenced = false;
	std::exception_ptr injected_worker_failure, injected_maintenance_failure;
	bool fail_settled_worker_wait = false;
	bool fail_worker_wait = false, close_after_session_open = false;
	d::RetirementHandle<RecoveryResult> base;
	std::optional<l::Descriptor> confirmed_prepared;
	std::shared_ptr<detail::CoordinatorAccess::PreparedProbePause> prepared_probe_pause;
	d::StorageOwner<l::ImageBuilder> builder;
	d::RetirementHandle<PagedCandidateResult> paged_candidate;
	bool paged_writer = options.image_format == LocalImageFormat::PagedV2;
	bool pending_paged_confirmation = false, compaction_requested = false;
	bool compaction_admission_refused = false, fail_compaction_admission = false;
	std::uint64_t completed_compactions = 0;
	std::chrono::steady_clock::time_point next_compaction_check{};
	task::cancel_source network_cancel;
	std::optional<asio::steady_timer> candidate_wake, disk_wake, scrub_wake;
	std::optional<l::detail::PackScrub> scrubber;
	d::RetirementHandle<l::detail::LeasedPagedRoot> scrub_root;
	std::uint64_t cancellation = 0, candidate_cancellation = 0;
	bool closing = false, running = false, settled = false, publication_started = false;
	// The candidate loop asks upstream for a Replace while a repair is pending (diagnostic.repair.pending).
	bool force_snapshot = false, repair_woke = false;
	std::chrono::steady_clock::time_point publication_retry{};
	Impl(CoordinatorOptions config, std::shared_ptr<l::DiskStore> disk,
		 std::shared_ptr<NativeDriver> completion, d::RetirementDomain &retirement, asio::thread_pool &pool)
		: options(std::move(config)),
		  sources(options.authority_endpoints, options.source.endpoint, options.quorum_refresh,
				  options.reconnect_seed ^ UINT64_C(0x6a09e667f3bcc909)),
		  store(std::move(disk)), driver(std::move(completion)), domain(retirement), worker(pool),
		  base(domain.make<RecoveryResult>(options)) {
		if (options.completion_port && !options.completion_port->uses(driver))
			throw std::invalid_argument("coordinator completion port driver mismatch");
	}
	// Asks for a Replace and a repair publication of the selected generation; a network wait in progress ends so
	// the loop can ask. Idempotent while pending, so later reports do not cancel the Replace being received.
	void request_repair(bool wake) noexcept {
		if (!paged_writer)
			return;
		if (diagnostic.repair.pending)
			return;
		diagnostic.repair.pending = true;
		++diagnostic.repair.requested;
		if (wake && diagnostic.phase == CoordinatorPhase::Receiving) {
			repair_woke = true;
			network_cancel.request();
		}
		notify_candidate();
	}
	void stop_construction() noexcept {
		if (construction) {
			construction->stop.store(true, std::memory_order_release);
			steps_at_stop = construction->steps.load(std::memory_order_relaxed);
		}
	}
	void settle_construction(ConstructionControl &control) noexcept {
		construction_stopped = control.stop.load(std::memory_order_acquire);
		settled_construction_steps = control.steps.load(std::memory_order_relaxed);
		construction = nullptr;
	}
	bool cancelled() const noexcept { return closing || candidate_cancellation != cancellation; }
	void fence(std::exception_ptr failure) noexcept {
		if (!terminal_fenced) {
			terminal_fenced = true;
			if (options.publication_sink)
				options.publication_sink->fence();
			terminal_failure = failure;
			++diagnostic.failures;
		}
		diagnostic.error = terminal_failure;
		diagnostic.phase = CoordinatorPhase::Fenced;
		close();
	}
	std::chrono::milliseconds next_reconnect_delay(ReconnectBackoff &backoff) noexcept {
		reconnect_observation.delay = backoff.next();
		++reconnect_observation.count;
		return reconnect_observation.delay;
	}
	task::task<void> pause(std::chrono::milliseconds duration) {
		co_await wait_candidate(std::chrono::steady_clock::now() + duration);
	}
	task::task<void> wait_candidate(std::chrono::steady_clock::time_point expires) {
		candidate_wake->expires_at(expires);
		const asio::error_code error = co_await task::await_asio<void(asio::error_code)>(
			[&](auto handler) { candidate_wake->async_wait(std::move(handler)); });
		if (error && error != asio::error::operation_aborted)
			throw asio::system_error(error);
	}
	void notify_candidate() noexcept {
		if (candidate_wake)
			candidate_wake->cancel();
	}
	void notify_disk() noexcept {
		if (disk_wake)
			disk_wake->cancel();
	}
	task::task<void> wait_disk() {
		auto expires = std::chrono::steady_clock::time_point::max();
		if (diagnostic.phase == CoordinatorPhase::WaitingPublication && !cancelled() &&
			publication_retry > std::chrono::steady_clock::now())
			expires = publication_retry;
		disk_wake->expires_at(expires);
		const asio::error_code error = co_await task::await_asio<void(asio::error_code)>(
			[&](auto handler) { disk_wake->async_wait(std::move(handler)); });
		if (error && error != asio::error::operation_aborted)
			throw asio::system_error(error);
	}
	task::task<d::RetirementHandle<RecoveryResult>> recover(bool selected) {
		auto result = base;
		result->failure = {};
		if (selected)
			result->prepared = false;
		result->ready.store(false, std::memory_order_relaxed);
		std::optional<l::detail::ManagedRecoveryContext> recovery;
		if (selected) {
			recovery.emplace(l::detail::ManagedRecovery::capture(*store));
			for (auto *context : {&recovery->legacy, &recovery->paged}) {
				auto &limits = context->limits;
				limits.bytes = std::min(limits.bytes, result->limits.bytes);
				limits.strings = std::min(limits.strings, result->limits.strings);
				limits.namespaces = std::min(limits.namespaces, result->limits.namespaces);
				limits.records = std::min(limits.records, result->limits.records);
				limits.overrides = std::min(limits.overrides, result->limits.overrides);
			}
		}
		// One candidate coroutine admits at most one configuration-worker task.
		// The task retains its result through authentic worker completion even
		// when request interest has been cancelled on the owner executor.
		const auto settlement = recovery;
		asio::post(worker, [result, selected, recovery = std::move(recovery),
							probe_pause = selected ? std::exchange(prepared_probe_pause, {}) : nullptr,
							injected = std::exchange(injected_worker_failure, {})]() mutable {
			try {
				if (injected)
					std::rethrow_exception(injected);
				if (selected) {
					auto candidate =
						l::detail::ManagedRecovery::recover(result->directory, std::move(*recovery));
					result->generation.emplace(std::move(candidate));
					if (probe_pause) {
						probe_pause->reached.store(true, std::memory_order_release);
						probe_pause->resume.wait();
					}
					if (l::detail::StateCursor::logical_commitment(*result->generation))
						result->prepared = l::detail::PreparedReader::metadata_ready(
							result->directory, *result->generation, ::geteuid());
				}
				if (RAND_bytes(reinterpret_cast<unsigned char *>(result->next_id.data()),
							   result->next_id.size()) != 1)
					throw std::runtime_error("managed generation identity generation failed");
			} catch (...) {
				result->failure = std::current_exception();
			}
			result->ready.store(true, std::memory_order_release);
		});
		diagnostic.worker_pending = true;
		try {
			if (fail_worker_wait) {
				fail_worker_wait = false;
				throw std::bad_alloc();
			}
			while (!result->ready.load(std::memory_order_acquire))
				co_await pause(std::chrono::milliseconds(1));
			if (std::exchange(fail_settled_worker_wait, false))
				throw std::bad_alloc();
		} catch (...) {
			// Scheduler failure cannot manufacture authentic worker settlement.
			if (!result->ready.load(std::memory_order_acquire))
				std::terminate();
			diagnostic.worker_pending = false;
			if (result->failure) {
				if (settlement)
					l::detail::ManagedRecovery::settle_failure(*store, *settlement, result->failure);
				std::rethrow_exception(result->failure);
			}
			throw;
		}
		diagnostic.worker_pending = false;
		if (result->failure) {
			if (settlement)
				l::detail::ManagedRecovery::settle_failure(*store, *settlement, result->failure);
			std::rethrow_exception(result->failure);
		}
		co_return result;
	}
	task::task<l::detail::StateEquivalenceProof> compare_candidate() {
		if (!base->generation || !builder || !builder->done())
			throw std::logic_error("same-cursor comparison lacks completed input");
		auto input = l::detail::CandidateFlatInput::capture(*builder);
		auto source = *base->generation;
		const auto descriptor = source.descriptor();
		const auto directory = source.directory_identity();
		auto factory = domain.factory();
		auto heap = domain.storage_account();
		auto limits = options.view_limits;
		limits.retirement_domain = nullptr;
		auto result = factory.make<CandidateComparisonResult>();
		asio::post(worker, [result, input = std::move(input), source = std::move(source), factory, heap,
							limits]() mutable {
			try {
				l::detail::CandidateFlatValidator validator(std::move(input), factory, heap, limits);
				while (!validator.done()) {
					result->control.step();
					l::ValidationBudget budget;
					validator.step(budget);
				}
				l::detail::StateEquivalence equality(l::detail::StateCursor(std::move(source), heap),
													 l::detail::StateCursor(validator.take(), heap));
				while (!equality.done()) {
					result->control.step();
					l::ValidationBudget budget;
					equality.step(budget);
				}
				if (!equality.equal())
					throw std::invalid_argument("same-cursor managed state conflict");
				result->proof.emplace(equality.take_proof());
			} catch (...) {
				result->failure = std::current_exception();
			}
			result->ready.store(true, std::memory_order_release);
		});
		construction = &result->control;
		diagnostic.worker_pending = true;
		try {
			while (!result->ready.load(std::memory_order_acquire))
				co_await pause(std::chrono::milliseconds(1));
		} catch (...) {
			if (!result->ready.load(std::memory_order_acquire))
				std::terminate();
			settle_construction(result->control);
			diagnostic.worker_pending = false;
			if (result->failure)
				std::rethrow_exception(result->failure);
			throw;
		}
		settle_construction(result->control);
		diagnostic.worker_pending = false;
		if (result->failure)
			std::rethrow_exception(result->failure);
		if (store->selection().descriptor != descriptor || store->directory_identity() != directory)
			throw std::invalid_argument("same-cursor comparison base changed");
		co_return std::move(*result->proof);
	}

	task::task<void> build_paged(const ReceivedUpdate *update, bool compact = false,
									  bool force = false, bool repair = false) {
		auto result = domain.make<PagedCandidateResult>();
		result->compaction = compact;
		std::optional<ReceivedUpdate> source;
		if (update)
			source = *update;
		auto actual = base->generation;
		result->selected_base = actual ? std::optional(actual->descriptor()) : std::nullopt;
		const auto generation = base->next_id;
		const auto context = l::detail::ManagedRecovery::capture(*store);
		auto factory = domain.factory();
		auto heap = domain.storage_account();
		auto limits = options.view_limits;
		limits.retirement_domain = nullptr;
		asio::post(worker, [result, source = std::move(source), actual = std::move(actual), generation,
							directory = context.directory, storage = context.storage,
							prior = context.paged.prior, compact, force, repair, factory, heap, limits]() mutable {
			try {
				if (!source && compact) {
					if (!actual || !prior || prior->descriptor() != actual->descriptor())
						throw std::invalid_argument("compaction lacks actual captured head");
					std::array<l::StorageIdentity, 8> selected{};
					std::size_t count = 0;
					const auto inputs = prior->registry()->inputs();
					for (std::size_t i = 0; i < inputs.size() && count < selected.size(); ++i) {
						const auto live = prior->registry()->reachability(i);
						if (live.pages && (force || live.bytes < inputs[i].ownership.length / 2)) {
							selected[count++] = inputs[i].ownership.segment;
							if (force)
								break;
						}
					}
					if (!count)
						result->descriptor = actual->descriptor();
					else {
						const auto bounds = l::detail::paged_limits(limits, l::paged::root_bytes);
						l::detail::PagedCompactionBuilder candidate(*actual, {selected.data(), count},
																	generation, factory, heap,
																	l::detail::CompactionLimits{}, bounds);
						while (!candidate.done()) {
							result->control.step();
							l::ValidationBudget budget;
							candidate.step(budget);
						}
						result->maintenance = candidate.evidence();
						result->plan.emplace(candidate.take());
						result->descriptor = result->plan->descriptor();
					}

				} else if (!source) {
					if (!actual)
						throw std::invalid_argument("migration requires actual recovered base");
					l::detail::PagedMaintenanceBuilder candidate(*actual, factory, heap, storage, generation,
																 limits);
					while (!candidate.done()) {
						result->control.step();
						l::ValidationBudget budget;
						candidate.step(budget);
					}
					result->maintenance = candidate.evidence();
					result->plan.emplace(candidate.take());
					result->descriptor = result->plan->descriptor();
				} else if (source->snapshot) {
					l::detail::PagedSnapshotPlanBuilder candidate(source->snapshot, actual, directory,
																  storage, generation, factory, heap, limits, repair);
					while (!candidate.done()) {
						result->control.step();
						l::ValidationBudget budget;
						candidate.step(budget);
					}
					if (candidate.unchanged()) {
						result->unchanged.emplace(candidate.take_unchanged_proof());
						result->descriptor = actual->descriptor();
					} else {
						result->plan.emplace(candidate.take());
						result->descriptor = result->plan->descriptor();
						result->repair = result->plan->intent() == l::detail::PagedPlanIntent::Repair;
						result->repair_in_place = result->repair && actual &&
												  result->descriptor->cursor == actual->descriptor().cursor;
					}
				} else {
					if (!actual || !l::detail::StateCursor::logical_commitment(*actual))
						throw std::invalid_argument("paged delta requires a migrated actual base");
					if (source->kind == d::UpdateKind::Progress) {
						l::detail::PagedPlanBuilder candidate(*actual, source->delta, generation, factory,
															  heap);
						while (!candidate.done()) {
							result->control.step();
							l::ValidationBudget budget;
							candidate.step(budget);
						}
						result->plan.emplace(candidate.take());
					} else {
						const auto bounds = l::detail::paged_limits(limits, l::paged::root_bytes);
						l::detail::PagedDeltaPlanBuilder candidate(*actual, source->delta, generation,
																   factory, heap, bounds);
						while (!candidate.done()) {
							result->control.step();
							l::ValidationBudget budget;
							candidate.step(budget);
						}
						result->plan.emplace(candidate.take());
					}
					result->descriptor = result->plan->descriptor();
				}
			} catch (...) {
				result->failure = std::current_exception();
			}
			result->ready.store(true, std::memory_order_release);
		});
		construction = &result->control;
		diagnostic.worker_pending = true;
		try {
			while (!result->ready.load(std::memory_order_acquire))
				co_await pause(std::chrono::milliseconds(1));
		} catch (...) {
			if (!result->ready.load(std::memory_order_acquire))
				std::terminate();
			settle_construction(result->control);
			diagnostic.worker_pending = false;
			if (result->failure)
				std::rethrow_exception(result->failure);
			throw;
		}
		settle_construction(result->control);
		diagnostic.worker_pending = false;
		if (result->failure)
			std::rethrow_exception(result->failure);
		if (store->selection().descriptor != result->selected_base ||
			store->directory_identity() != context.directory)
			throw std::invalid_argument("paged construction actual base changed");
		paged_candidate = std::move(result);
	}

	void bind_base(d::RetirementHandle<RecoveryResult> recovered) {
		const auto actual = store->selection();
		if (actual.knowledge == l::SelectionKnowledge::Uncertain)
			throw std::runtime_error("managed selection requires startup recovery");
		if (actual.knowledge == l::SelectionKnowledge::Known) {
			if (!recovered->generation || recovered->generation->descriptor() != actual.descriptor ||
				recovered->generation->directory_identity() != store->directory_identity())
				throw std::runtime_error("managed recovery differs from actual selection");
			const auto &cursor = recovered->generation->descriptor().cursor;
			if (!cursor.same_identity(options.source.configured) &&
				(!options.reset_from || !cursor.same_identity(*options.reset_from)))
				throw std::invalid_argument("managed identity transition is not configured");
			diagnostic.recoverable = cursor;
		} else {
			if (recovered->generation)
				throw std::runtime_error("unexpected managed recovery for absent selection");
			diagnostic.recoverable.reset();
		}
		bool equivalent = false, repaired = false;
		if (pending_paged_confirmation) {
			if (!paged_candidate)
				throw std::invalid_argument("paged recovered selection differs from candidate");
			if (recovered->generation && recovered->generation->descriptor() == paged_candidate->descriptor) {
				store->confirm_recovered(*recovered->generation);
				equivalent = bool(paged_candidate->maintenance);
				if (equivalent && paged_candidate->compaction)
					++completed_compactions;
				if (paged_candidate->repair) {
					repaired = paged_candidate->repair_in_place;
					diagnostic.repair.pending = false;
					++diagnostic.repair.completed;
					// The damaged generation is now unreachable: collect its files.
					store->request_collection();
				}
			} else if (actual.descriptor != paged_candidate->selected_base)
				throw std::invalid_argument("paged rejection changed actual selection");
			pending_paged_confirmation = false;
			notify_disk();
		}
		base = std::move(recovered);
		confirmed_prepared.reset();
		if (base->prepared && base->generation &&
			base->generation->descriptor().cursor.same_identity(options.source.configured))
			confirmed_prepared = base->generation->descriptor();
		if (options.publication_sink && base->generation) {
			if (equivalent)
				options.publication_sink->selected_equivalent(*base->generation,
															  *paged_candidate->maintenance);
			else if (repaired)
				options.publication_sink->selected_repaired(*base->generation);
			else
				options.publication_sink->selected(*base->generation);
		}
	}
	task::task<bool> publish_candidate() {
		compaction_admission_refused = false;
		diagnostic.phase = CoordinatorPhase::WaitingPublication;
		notify_disk();
		const auto expires = std::chrono::steady_clock::now() + options.source.timeout;
		while (!cancelled() && !publication_started && !compaction_admission_refused) {
			if (std::chrono::steady_clock::now() >= expires)
				throw std::runtime_error("managed publication admission deadline");
			co_await wait_candidate(expires);
		}
		while (publication_started && store->publication_status() == l::DiskPublicationStatus::Preparing)
			co_await wait_candidate(std::chrono::steady_clock::time_point::max());
		if (!cancelled() && store->publication_status() == l::DiskPublicationStatus::Published)
			co_return true;
		if (!cancelled() && store->publication_error())
			std::rethrow_exception(store->publication_error());
		co_return false;
	}
	task::task<void> recover_maintenance(bool changed) {
		try {
			if (injected_maintenance_failure) {
				injected_worker_failure = std::exchange(injected_maintenance_failure, {});
				fail_settled_worker_wait = true;
			}
			auto recovered = co_await recover(changed);
			if (!closing)
				bind_base(std::move(recovered));
		} catch (...) {
			throw MaintenanceFailure{std::current_exception()};
		}
	}
	task::task<void> migrate_flat_head() {
		// Only a flat (layout 1) base migrates; a paged base is already in the writer's format.
		if (!paged_writer || !base->generation ||
			l::detail::StateCursor::logical_commitment(*base->generation) ||
			!base->generation->descriptor().cursor.same_identity(options.source.configured))
			co_return;
		diagnostic.phase = CoordinatorPhase::Building;
		co_await build_paged(nullptr);
		if (!cancelled())
			co_await publish_candidate();
		if (closing)
			co_return;
		const auto actual = store->selection();
		const bool changed = actual.descriptor && base->generation->descriptor() != actual.descriptor;
		co_await recover_maintenance(changed);
		paged_candidate.reset();
		publication_started = false;
		publication_retry = {};
	}

	task::task<void> compact_head() {
		const auto now = std::chrono::steady_clock::now();
		if (!paged_writer || diagnostic.repair.pending ||
			(!compaction_requested && (options.compaction_interval == std::chrono::milliseconds::zero() ||
									   now < next_compaction_check)) ||
			!base->generation || !l::detail::StateCursor::logical_commitment(*base->generation))
			co_return;
		const bool force = std::exchange(compaction_requested, false);
		next_compaction_check = now + options.compaction_interval;
		diagnostic.phase = CoordinatorPhase::Building;
		std::exception_ptr refusal;
		try {
			co_await build_paged(nullptr, true, force);
		} catch (const d::StoragePressure &) {
			refusal = std::current_exception();
		} catch (const std::bad_alloc &) {
			refusal = std::current_exception();
		} catch (const std::length_error &) {
			refusal = std::current_exception();
		}
		if (refusal) {
			l::detail::ManagedRecovery::yield_optional(*store);
			++diagnostic.failures;
			diagnostic.error = refusal;
			paged_candidate.reset();
			co_return;
		}
		if (!paged_candidate->plan) {
			paged_candidate.reset();
			co_return;
		}
		if (!cancelled())
			co_await publish_candidate();
		if (compaction_admission_refused && !publication_started) {
			paged_candidate.reset();
			publication_retry = {};
			co_return;
		}
		if (closing)
			co_return;
		const auto actual = store->selection();
		const bool changed = actual.descriptor && base->generation->descriptor() != actual.descriptor;
		co_await recover_maintenance(changed);
		paged_candidate.reset();
		publication_started = false;
		publication_retry = {};
	}

	void offer_publication() {
		if (diagnostic.phase != CoordinatorPhase::WaitingPublication || cancelled() ||
			!store->publication_ready() || compaction_admission_refused ||
			std::chrono::steady_clock::now() < publication_retry)
			return;
		const auto actual = store->selection();
		if (paged_candidate) {
			if (paged_candidate->compaction && std::exchange(fail_compaction_admission, false))
				throw d::StoragePressure("injected compaction admission pressure");
			if (!paged_candidate->plan)
				throw std::logic_error("paged candidate has no publication plan");
			if (paged_candidate->plan->intent() == l::detail::PagedPlanIntent::TrustedReset) {
				if (!actual.descriptor || !options.reset_from ||
					!actual.descriptor->cursor.same_identity(*options.reset_from))
					throw std::invalid_argument("managed paged reset is not configured");
				store->begin_reset(std::move(*paged_candidate->plan),
								   {*actual.descriptor, options.source.configured, {}});
			} else
				store->begin(std::move(*paged_candidate->plan));
			pending_paged_confirmation = true;
		} else if (actual.knowledge == l::SelectionKnowledge::Known &&
				   !actual.descriptor->cursor.same_identity(options.source.configured)) {
			if (!options.reset_from || !actual.descriptor->cursor.same_identity(*options.reset_from))
				throw std::invalid_argument("managed reset is not configured");
			store->begin_reset(*builder, {*actual.descriptor, options.source.configured, {}});
		} else {
			store->begin(*builder,
						 actual.descriptor ? std::optional(actual.descriptor->cursor) : std::nullopt);
		}
		publication_started = true;
		diagnostic.phase = CoordinatorPhase::Publishing;
		notify_candidate();
	}
	// One owner-executor task, paced by its own timer: the scrub hashes the selected generation's packs
	// at options.scrub_bytes_per_second and starts the next pass as one ends.
	task::task<void> scrub_loop() {
		try {
			co_await scrub_loop_body();
		} catch (...) {
			std::terminate();
		}
	}
	task::task<void> scrub_loop_body() {
		if (!options.scrub_bytes_per_second)
			co_return;
		constexpr auto tick = std::chrono::milliseconds(20);
		constexpr std::uint64_t per_tick = 2 * 1024 * 1024;
		scrubber.emplace(options.scrub_bytes_per_second);
		std::uint64_t reported_passes = 0;
		while (!closing) {
			const auto now = std::chrono::steady_clock::now();
			// The recovery worker rewrites base->generation until `ready`; the loop never reads it before.
			if (diagnostic.initialized && base->ready.load(std::memory_order_acquire) && base->generation) {
				auto root = l::detail::ScrubSource::root(*base->generation);
				if (root && root.get() != scrub_root.get()) {
					scrub_root = std::move(root);
					scrubber->begin(scrub_root->registry()->inputs(), now, options.directory);
					reported_passes = scrubber->passes();
					diagnostic.scrub.damaged_packs = 0;
					diagnostic.scrub.source_failure = false;
				} else if (scrub_root && !scrubber->running())
					scrubber->begin(scrub_root->registry()->inputs(), now, options.directory);
				if (scrub_root) {
					scrubber->step(now, per_tick);
					diagnostic.scrub.passes = scrubber->passes();
					diagnostic.scrub.bytes_hashed = scrubber->bytes_hashed();
					// One report per completed pass: the packs that pass found, not a running total.
					if (scrubber->passes() != reported_passes) {
						reported_passes = scrubber->passes();
						diagnostic.scrub.damaged_packs = scrubber->failures().size();
						diagnostic.scrub.source_failure = !scrubber->failures().empty();
						if (diagnostic.scrub.source_failure) {
							++diagnostic.failures;
							diagnostic.error = std::make_exception_ptr(std::runtime_error(
								"scrub: a pack of the selected generation differs from its owner record"));
							request_repair(true);
						}
					}
				}
			}
			scrub_wake->expires_after(tick);
			const asio::error_code error = co_await task::await_asio<void(asio::error_code)>(
				[&](auto handler) { scrub_wake->async_wait(std::move(handler)); });
			if (error && error != asio::error::operation_aborted)
				throw asio::system_error(error);
		}
		scrub_root.reset();
	}
	task::task<void> disk_loop() {
		try {
			co_await disk_loop_body();
		} catch (...) {
			std::terminate();
		}
	}
	task::task<void> disk_loop_body() {
		unsigned burst = 0;
		while (!store->drained()) {
			// Publication gets the settled boundary before advance resumes GC.
			std::exception_ptr failure;
			try {
				offer_publication();
			} catch (const l::DiskPressure &) {
				store->request_collection();
				publication_retry = std::chrono::steady_clock::now() + options.poll_interval;
				compaction_admission_refused = paged_candidate && paged_candidate->compaction;
			} catch (const d::StoragePressure &) {
				store->request_collection();
				publication_retry = std::chrono::steady_clock::now() + options.poll_interval;
				compaction_admission_refused = paged_candidate && paged_candidate->compaction;
			} catch (const std::bad_alloc &) {
				store->request_collection();
				publication_retry = std::chrono::steady_clock::now() + options.poll_interval;
				compaction_admission_refused = paged_candidate && paged_candidate->compaction;
			} catch (const SessionResetError &error) {
				failure = std::current_exception();
				if (options.publication_sink && fatal_source_reset(error.reason()))
					options.publication_sink->fence();
			} catch (...) {
				failure = std::current_exception();
			}
			if (failure) {
				fence(failure);
			}
			store->advance();
			notify_candidate();
			if (auto operation = store->operation()) {
				try {
					if (options.completion_port)
						co_await options.completion_port->run_one(operation);
					else
						co_await driver->run_one(operation);
				} catch (...) {
					// A failed original driver cannot manufacture settlement.
					// Follow the native authority driver's fail-stop policy.
					std::terminate();
				}
				if (store->error() || store->publication_status() == l::DiskPublicationStatus::Fenced)
					fence(store->error() ? store->error() : store->publication_error());
				notify_candidate();
				if (++burst == 16) {
					burst = 0;
					co_await task::yield();
				}
			} else if (store->needs_work()) {
				co_await task::yield();
			} else {
				burst = 0;
				if (store->status() == l::StoreStatus::Fenced)
					fence(store->error() ? store->error() : store->publication_error());
				if (!store->drained())
					co_await wait_disk();
			}
		}
	}
	void close() noexcept {
		stop_construction();
		closing = true;
		if (options.publication_sink)
			options.publication_sink->fence();
		++cancellation;
		network_cancel.request();
		notify_candidate();
		notify_disk();
		if (scrub_wake)
			scrub_wake->cancel();
		if (store)
			store->close();
	}
	void record_cleanup(std::size_t index, std::chrono::steady_clock::time_point started,
						std::size_t bytes, bool done) noexcept {
		auto &slice = diagnostic.cleanup[index];
		if (slice.last_resume != std::chrono::steady_clock::time_point{})
			slice.maximum_gap_ns = std::max(slice.maximum_gap_ns, static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(started - slice.last_resume).count()));
		slice.last_resume = started;
		++slice.turns;
		slice.charged_bytes += bytes;
		if (done)
			slice.finished = std::chrono::steady_clock::now();
	}
	void acknowledge(const d::Cursor &cursor) noexcept {
		diagnostic.acknowledged = cursor;
		diagnostic.publication_acknowledged_at = std::chrono::steady_clock::now();
	}
	task::task<void> discard(std::optional<ReceivedUpdate> &update) {
		diagnostic.phase = CoordinatorPhase::Discarding;
		diagnostic.discard_started_at = std::chrono::steady_clock::now();
		diagnostic.cleanup = {};
		if (options.publication_sink) {
			for (;;) {
				const auto started = std::chrono::steady_clock::now();
				d::WorkBudget budget;
				const auto before = budget.bytes;
				const bool done = options.publication_sink->release_stage(budget);
				record_cleanup(0, started, before - budget.bytes, done);
				if (done)
					break;
				co_await task::yield();
			}
		}
		if (builder) {
			for (;;) {
				const auto started = std::chrono::steady_clock::now();
				l::ValidationBudget budget;
				const auto before = budget.bytes;
				const bool done = builder->discard(budget);
				record_cleanup(1, started, before - budget.bytes, done);
				if (done)
					break;
				co_await task::yield();
			}
			builder.reset();
		}
		if (update) {
			for (;;) {
				const auto started = std::chrono::steady_clock::now();
				d::WorkBudget budget;
				const auto before = budget.bytes;
				const bool done = update->snapshot ? update->snapshot->discard(budget)
												: update->delta->discard(budget);
				record_cleanup(2, started, before - budget.bytes, done);
				if (done)
					break;
				co_await task::yield();
			}
			update.reset();
		}
	}
	task::task<void> candidate_loop();
};

namespace {
task::task<void> release_session(ClientOptions options, SessionHandle &session, bool strict) {
	options.timeout = std::min(options.timeout, std::chrono::milliseconds(1000));
	try {
		co_await close_session(std::move(options), session);
	} catch (const SourceTrustError &) {
		throw;
	} catch (const SessionResetError &error) {
		if (fatal_source_reset(error.reason()))
			throw;
	} catch (const std::invalid_argument &) {
		if (strict)
			throw;
	} catch (...) {
	}
}
} // namespace
task::task<void> Coordinator::Impl::candidate_loop() {
	SessionHandle session;
	auto opening_endpoint = options.source.endpoint;
	auto session_source = [&] {
		auto source = options.source;
		source.endpoint = opening_endpoint;
		return source;
	};
	bool revalidating = false;
	std::optional<PublicationFeedback> feedback;
	bool session_forced = false;
	ReconnectBackoff reconnect(options.poll_interval, options.reconnect_maximum, options.reconnect_seed);
	std::exception_ptr fatal;
	try {
		if (!options.source.connections)
			options.source.connections =
				make_connection_cache(options.source.scheduler->executor(), 1, options.source.limits.storage_account);
		while (!closing && store->status() == l::StoreStatus::Starting)
			co_await wait_candidate(std::chrono::steady_clock::time_point::max());
		if (!closing) {
			diagnostic.phase = CoordinatorPhase::Recovering;
			auto recovered = co_await recover(store->selection().knowledge == l::SelectionKnowledge::Known);
			if (!closing) {
				bind_base(std::move(recovered));
				diagnostic.initialized = true;
			}
		}
		while (!closing) {
			candidate_cancellation = cancellation;
			network_cancel = task::cancel_source{};
			publication_started = false;
			paged_candidate.reset();
			publication_retry = {};
			std::optional<ReceivedUpdate> update;
			std::optional<PublicationFeedback> offered;
			bool reset_session = false, idle = false, offered_snapshot = false;
			bool network_attempt = false, unavailable = false, round_complete = true;
			std::exception_ptr failure;
			try {
				co_await migrate_flat_head();
				co_await compact_head();
				if (cancelled())
					throw asio::system_error(asio::error::operation_aborted);
				if (diagnostic.repair.pending) {
					// A repair needs a Replace: a session that was not opened for one is closed and reopened.
					force_snapshot = true;
					if (session.valid() && !session_forced) {
						co_await release_session(session_source(), session, sources.pooled());
						session = {};
						feedback.reset();
					}
				}
				if (session.valid() && sources.refresh_due(std::chrono::steady_clock::now())) {
					revalidating = true;
					co_await release_session(session_source(), session, sources.pooled());
					session = {};
					feedback.reset();
				}
				auto request = options.source;
				request.endpoint = sources.endpoint();
				const auto actual = store->selection();
				request.base =
					actual.descriptor && actual.descriptor->cursor.same_identity(request.configured)
						? std::optional(actual.descriptor->cursor)
						: std::nullopt;
				if (diagnostic.repair.pending && options.delivery_mode == DeliveryMode::Poll)
					request.base.reset();
				diagnostic.phase = CoordinatorPhase::Receiving;
				if (options.delivery_mode == DeliveryMode::Subscription) {
					if (!session.valid()) {
						network_attempt = true;
						session = co_await open_session(request, force_snapshot, network_cancel.token());
						session_forced = force_snapshot;
						opening_endpoint = request.endpoint;
						const auto opened = std::chrono::steady_clock::now();
						sources.opened(opened);
						if (sources.pooled())
							diagnostic.last_quorum_open = opened;
						revalidating = false;
						feedback.reset();
						if (options.publication_sink && !cancelled())
							options.publication_sink->authenticated();
						if (std::exchange(close_after_session_open, false))
							close();
					}
					if (cancelled())
						throw asio::system_error(asio::error::operation_aborted);
					const auto wait =
						sources.wait(std::chrono::steady_clock::now(),
									 std::min({session.maximum_wait(), std::chrono::milliseconds(30000),
											   request.timeout / 2}));
					network_attempt = true;
					auto event = co_await next_session(request, session, feedback, wait, network_cancel.token());
					if (auto *offer = std::get_if<SessionOffer>(&event)) {
						offered = PublicationFeedback{offer->window, offer->update.target};
						offered_snapshot = offer->update.kind == d::UpdateKind::Snapshot;
						update.emplace(std::move(offer->update));
					} else if (std::holds_alternative<SessionIdle>(event)) {
						idle = true;
						sources.successful();
						reconnect.reset();
					} else {
						const auto reason = std::get<SessionReset>(event).reason;
						if (sources.pooled() && (reason == SessionResetReason::NotLeader ||
												 reason == SessionResetReason::Unavailable ||
												 reason == SessionResetReason::SourceBehind))
							throw SessionResetError(reason);
						if (reason == SessionResetReason::Forbidden ||
							reason == SessionResetReason::Unauthorized ||
							reason == SessionResetReason::IdentityChanged ||
							reason == SessionResetReason::InvalidRequest)
							throw SessionResetError(reason);
						reset_session = !session.valid();
						force_snapshot = force_snapshot || reason == SessionResetReason::HistoryLost ||
										 reason == SessionResetReason::RepresentationLimit;
					}
					network_attempt = false;
					if (!update && !cancelled()) {
						if (reset_session) {
							co_await release_session(session_source(), session, sources.pooled());
							session = {};
							feedback.reset();
						}
						if (idle && sources.pooled() && wait == std::chrono::milliseconds::zero())
							co_await task::yield();
						else if (!idle || wait == std::chrono::milliseconds::zero())
							co_await pause(idle ? options.poll_interval : next_reconnect_delay(reconnect));
						continue;
					}
				} else {
					network_attempt = true;
					update.emplace(co_await receive(std::move(request), network_cancel.token()));
				}
				network_attempt = false;
				sources.successful();
				if (!update)
					throw asio::system_error(asio::error::operation_aborted);
				diagnostic.received = update->target;
				diagnostic.validated = update->target;
				// A repair at the selected cursor replaces the selected generation by an equal one: the sink keeps
				// its cache and receives selected_repaired.
				const bool repair_in_place = diagnostic.repair.pending && paged_writer && update->snapshot &&
											 actual.descriptor && update->target == actual.descriptor->cursor;
				if (!cancelled()) {
					if (options.publication_sink && !repair_in_place) {
						options.publication_sink->begin_stage(*update);
						while (!cancelled()) {
							d::WorkBudget budget;
							if (options.publication_sink->stage_step(budget))
								break;
							co_await task::yield();
						}
					}
					if (cancelled())
						throw asio::system_error(asio::error::operation_aborted);
					diagnostic.phase = CoordinatorPhase::Building;
					auto account = store->storage_account();
					if (paged_writer)
						co_await build_paged(&*update, false, false, diagnostic.repair.pending);
					else {
						if (update->snapshot)
							builder = d::make_storage_owner<l::ImageBuilder>(account, std::malloc, std::free,
																			 update->snapshot, base->next_id,
																			 options.view_limits);
						else {
							if (!base->generation || base->generation->descriptor() != actual.descriptor)
								throw std::invalid_argument("managed delta has no exact recovered base");
							builder = d::make_storage_owner<l::ImageBuilder>(
								account, std::malloc, std::free, update->delta, *base->generation,
								base->next_id, options.view_limits);
						}
						while (!builder->done() && !cancelled()) {
							l::ValidationBudget budget;
							builder->step(budget);
							if (!builder->done())
								co_await task::yield();
						}
					}
					if (!cancelled()) {
						const auto candidate =
							paged_candidate ? *paged_candidate->descriptor : builder->descriptor();
						if (options.publication_sink && !repair_in_place)
							options.publication_sink->bind_candidate(candidate, store->directory_identity());
						if (actual.descriptor && candidate.cursor == actual.descriptor->cursor &&
							!(paged_candidate && paged_candidate->repair)) {
							if (!paged_candidate)
								co_await compare_candidate();
							else if (!paged_candidate->unchanged)
								throw std::invalid_argument(
									"same-cursor paged candidate lacks equality evidence");
							if (cancelled())
								throw asio::system_error(asio::error::operation_aborted);
							if (options.publication_sink)
								options.publication_sink->bind_candidate(*actual.descriptor,
																		 store->directory_identity());
							acknowledge(candidate.cursor);
						} else {
							if (co_await publish_candidate()) {
								if (!paged_candidate)
									acknowledge(candidate.cursor);
							}
						}
					}
				}
			} catch (const MaintenanceFailure &) {
				throw;
			} catch (const d::StoragePressure &) {
				l::detail::ManagedRecovery::yield_optional(*store);
				failure = std::current_exception();
			} catch (const std::bad_alloc &) {
				l::detail::ManagedRecovery::yield_optional(*store);
				failure = std::current_exception();
			} catch (const SessionResetError &error) {
				failure = std::current_exception();
				if (options.publication_sink && fatal_source_reset(error.reason()))
					options.publication_sink->fence();
			} catch (const l::detail::ReadSourceFailure &) {
				// A page of the selected generation failed first touch: ask for a Replace to repair it.
				failure = std::current_exception();
				request_repair(false);
			} catch (...) {
				failure = std::current_exception();
			}
			// The network wait that the scrub's repair request ended is not a failure.
			bool woken = false;
			if (failure && std::exchange(repair_woke, false)) {
				try {
					std::rethrow_exception(failure);
				} catch (const asio::system_error &error) {
					woken = error.code() == asio::error::operation_aborted;
				} catch (...) {
				}
			}
			repair_woke = false;
			if (failure && !cancelled() && !woken) {
				++diagnostic.failures;
				diagnostic.error = failure;
			}
			try {
				co_await discard(update);
			} catch (...) {
				std::terminate();
			}
			if (failure || cancelled()) {
				if (session.identity() != d::Identity{})
					co_await release_session(session_source(), session, sources.pooled());
				session = {};
				feedback.reset();
			}
			if (failure) {
				try {
					std::rethrow_exception(failure);
				} catch (const SourceTrustError &) {
					throw;
				} catch (const SessionResetError &error) {
					const auto reason = error.reason();
					unavailable = reason == SessionResetReason::NotLeader ||
								  reason == SessionResetReason::Unavailable ||
								  reason == SessionResetReason::SourceBehind;
					if (reason == SessionResetReason::Forbidden ||
						reason == SessionResetReason::Unauthorized ||
						reason == SessionResetReason::IdentityChanged ||
						reason == SessionResetReason::InvalidRequest)
						throw;
				} catch (const SourceUnavailableError &) {
					unavailable = network_attempt;
				} catch (const asio::system_error &error) {
					unavailable = network_attempt && error.code() != asio::error::operation_aborted;
				} catch (const std::invalid_argument &) {
					if (network_attempt && sources.pooled())
						throw;
				} catch (...) {
				}
			}
			if (unavailable && sources.pooled() && !cancelled()) {
				const auto previous = sources.index();
				round_complete = sources.unavailable();
				diagnostic.source_endpoint_index = sources.index();
				if (sources.index() != previous)
					++diagnostic.source_rotations;
				if (revalidating)
					++diagnostic.quorum_refresh_failures;
			}
			if (closing)
				break;
			diagnostic.phase = CoordinatorPhase::Reconciling;
			const auto actual = store->selection();
			if (actual.knowledge == l::SelectionKnowledge::Uncertain ||
				store->status() != l::StoreStatus::Ready)
				throw std::runtime_error("managed selection is not recoverable");
			const bool changed = actual.descriptor &&
								 (!base->generation || base->generation->descriptor() != actual.descriptor);
			diagnostic.recovery_started_at = std::chrono::steady_clock::now();
			auto recovered = co_await recover(changed);
			if (!closing) {
				bind_base(std::move(recovered));
				if (paged_candidate && !failure && !cancelled() && diagnostic.received && base->generation &&
					base->generation->descriptor().cursor == *diagnostic.received)
					acknowledge(*diagnostic.received);
			}
			if (!closing && options.delivery_mode == DeliveryMode::Poll && !failure && !cancelled() &&
				diagnostic.received && base->generation &&
				base->generation->descriptor().cursor == *diagnostic.received)
				reconnect.reset();
			if (!closing && options.delivery_mode == DeliveryMode::Subscription && session.valid() &&
				offered) {
				if (actual.descriptor && actual.descriptor->cursor == offered->cursor && base->generation &&
					base->generation->descriptor() == actual.descriptor && !failure && !cancelled()) {
					feedback = offered;
					reconnect.reset();
					if (offered_snapshot)
						force_snapshot = false;
				} else {
					co_await release_session(session_source(), session, sources.pooled());
					session = {};
					feedback.reset();
				}
			}
			if (!closing && !woken && (options.delivery_mode == DeliveryMode::Poll || !session.valid())) {
				if (sources.pooled() && unavailable && !round_complete)
					co_await task::yield();
				else
					co_await pause(options.delivery_mode == DeliveryMode::Poll &&
										   !(sources.pooled() && unavailable)
									   ? options.poll_interval
									   : next_reconnect_delay(reconnect));
			}
		}
	} catch (const MaintenanceFailure &error) {
		fatal = error.cause;
	} catch (...) {
		fatal = std::current_exception();
	}
	try {
		if (session.identity() != d::Identity{})
			co_await release_session(session_source(), session, sources.pooled());
	} catch (...) {
		if (!fatal)
			fatal = std::current_exception();
	}
	if (fatal) {
		if (diagnostic.worker_pending)
			std::terminate();
		fence(fatal);
	}
	options.source.connections.reset();
	close();
	if (options.publication_sink)
		options.publication_sink->fence();
	paged_candidate.reset();
	base.reset();
}

Coordinator::Coordinator(CoordinatorOptions options, std::shared_ptr<l::DiskStore> store,
						 std::shared_ptr<NativeDriver> driver, d::RetirementDomain &domain,
						 asio::thread_pool &worker) {
	if ((options.image_format != LocalImageFormat::FlatV1 &&
		 options.image_format != LocalImageFormat::PagedV2) ||
		options.compaction_interval < std::chrono::milliseconds::zero() ||
		options.compaction_interval > std::chrono::hours(24) ||
		(options.compaction_interval != std::chrono::milliseconds::zero() &&
		 options.compaction_interval < std::chrono::seconds(1)) ||
		!driver || options.directory.empty() || options.poll_interval <= std::chrono::milliseconds::zero() ||
		options.poll_interval > std::chrono::hours(24) ||
		options.reconnect_maximum < std::chrono::milliseconds::zero() ||
		options.reconnect_maximum > std::chrono::hours(24) ||
		(options.reconnect_maximum != std::chrono::milliseconds::zero() &&
		 options.reconnect_maximum < options.poll_interval) ||
		(store && store->storage_account() != domain.storage_account()))
		throw std::invalid_argument("invalid managed coordinator configuration");
	options.source.base.reset();
	options.source.limits.storage_account = domain.storage_account();
	options.view_limits.retirement_domain = &domain;
	impl_ = d::make_storage_owner<Impl>(domain.storage_account(), std::malloc, std::free, std::move(options),
										store, std::move(driver), domain, worker);
}
Coordinator::~Coordinator() = default;
detail::CoordinatorAccess::ReconnectObservation
detail::CoordinatorAccess::reconnect_observation(const Coordinator &coordinator) {
	return coordinator.impl_->reconnect_observation;
}
void detail::CoordinatorAccess::fail_worker_and_wait_after_ready(Coordinator &coordinator,
																 std::exception_ptr failure) {
	coordinator.impl_->injected_worker_failure = std::move(failure);
	coordinator.impl_->fail_settled_worker_wait = true;
}
void detail::CoordinatorAccess::fail_next_worker_wait(Coordinator &coordinator) {
	coordinator.impl_->fail_worker_wait = true;
}
void detail::CoordinatorAccess::close_after_next_session_open(Coordinator &coordinator) {
	coordinator.impl_->close_after_session_open = true;
}
void Coordinator::attach_store(std::shared_ptr<l::DiskStore> store) {
	if (!store || impl_->store || impl_->running ||
		store->storage_account() != impl_->domain.storage_account())
		throw std::logic_error("invalid managed startup store attachment");
	impl_->store = std::move(store);
	if (impl_->closing)
		impl_->store->close();
}
task::task<void> Coordinator::run() {
	if (impl_->running || !impl_->store)
		throw std::logic_error("managed coordinator already started");
	if (!impl_->options.source.scheduler)
		throw std::invalid_argument("managed coordinator requires its owner scheduler");
	auto owner = shared_from_this();
	impl_->running = true;
	return run_owned(std::move(owner));
}
task::task<void> Coordinator::run_owned(std::shared_ptr<Coordinator> owner) {
	try {
		const auto &executor = owner->impl_->options.source.scheduler->executor();
		owner->impl_->candidate_wake.emplace(executor);
		owner->impl_->disk_wake.emplace(executor);
		owner->impl_->scrub_wake.emplace(executor);
		co_await task::when_all(owner->impl_->disk_loop(), owner->impl_->candidate_loop(),
								owner->impl_->scrub_loop());
		owner->impl_->settled = true;
		owner->impl_->diagnostic.phase =
			owner->impl_->terminal_fenced ? CoordinatorPhase::Fenced : CoordinatorPhase::Closed;
	} catch (...) {
		// Group admission can fail after starting its first child. Never let
		// scheduler failure abandon a sibling's accepted original or worker.
		std::terminate();
	}
}

bool detail::CoordinatorAccess::cancelled_construction_settled(const Coordinator &coordinator) {
	const auto &impl = *coordinator.impl_;
	return !impl.construction && impl.construction_stopped &&
		   impl.settled_construction_steps <= impl.steps_at_stop + 1;
}
bool detail::CoordinatorAccess::retains_paged_candidate(const Coordinator &coordinator) {
	return static_cast<bool>(coordinator.impl_->paged_candidate);
}
void detail::CoordinatorAccess::pause_next_prepared_probe(Coordinator &coordinator,
                                                        std::shared_ptr<PreparedProbePause> pause) {
	if (!pause || !pause->resume.valid() || coordinator.impl_->prepared_probe_pause)
		throw std::invalid_argument("invalid prepared probe pause");
	coordinator.impl_->prepared_probe_pause = std::move(pause);
}
void detail::CoordinatorAccess::fail_next_maintenance_recovery(Coordinator &coordinator,
															   std::exception_ptr failure) {
	coordinator.impl_->injected_maintenance_failure = std::move(failure);
}
void detail::CoordinatorAccess::fail_next_compaction_admission(Coordinator &coordinator) {
	coordinator.impl_->fail_compaction_admission = true;
}
void detail::CoordinatorAccess::request_compaction(Coordinator &coordinator) {
	coordinator.impl_->compaction_requested = true;
	coordinator.impl_->notify_candidate();
}
void detail::CoordinatorAccess::request_repair(Coordinator &coordinator) {
	coordinator.impl_->request_repair(true);
}
std::uint64_t detail::CoordinatorAccess::completed_compactions(const Coordinator &coordinator) {
	return coordinator.impl_->completed_compactions;
}
void Coordinator::cancel_update() noexcept {
	impl_->stop_construction();
	++impl_->cancellation;
	impl_->network_cancel.request();
	impl_->notify_candidate();
	impl_->notify_disk();
	if (impl_->store)
		impl_->store->cancel_publication();
}
void Coordinator::close() noexcept { impl_->close(); }
bool Coordinator::drained() const noexcept {
	return impl_->settled && impl_->store && impl_->store->drained();
}
CoordinatorStatus Coordinator::status() const noexcept {
	auto status = impl_->diagnostic;
	if (impl_->store)
		status.selected = impl_->store->selection();
	status.prepared_ready = impl_->confirmed_prepared.has_value() && !impl_->closing &&
		status.phase != CoordinatorPhase::Fenced && status.phase != CoordinatorPhase::Closed &&
		status.phase != CoordinatorPhase::Closing && status.selected.knowledge == l::SelectionKnowledge::Known &&
		status.selected.descriptor == impl_->confirmed_prepared;
	return status;
}
std::shared_ptr<Coordinator> make_coordinator(CoordinatorOptions options, std::shared_ptr<l::DiskStore> store,
											  std::shared_ptr<NativeDriver> driver,
											  d::RetirementDomain &domain, asio::thread_pool &worker) {
	auto account = domain.storage_account();
	return std::allocate_shared<Coordinator>(d::StorageAllocator<Coordinator>(account), std::move(options),
											 std::move(store), std::move(driver), domain, worker);
}
std::shared_ptr<Coordinator> prepare_coordinator(CoordinatorOptions options,
												 std::shared_ptr<NativeDriver> driver,
												 d::RetirementDomain &domain, asio::thread_pool &worker) {
	return make_coordinator(std::move(options), {}, std::move(driver), domain, worker);
}
} // namespace detent::delivery
