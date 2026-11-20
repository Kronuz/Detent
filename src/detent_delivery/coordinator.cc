#include "paged_limits.hh"
#include "paged_page_access.hh"
#include "detent_delivery/coordinator.hh"
#include "coordinator_internal.hh"
#include "detent_delivery/reconnect_backoff.hh"
#include "detent_distribution/frame_input.hh"
#include "detent_local/derived_ids.hh"
#include "managed_recovery.hh"
#include "pack_scrub.hh"
#include "paged_compaction_plan.hh"
#include "paged_delta_plan.hh"
#include "paged_snapshot_plan.hh"
#include "scrub_source.hh"
#include "source_selection.hh"
#include "state_equivalence.hh"
#include <algorithm>
#include <asio.hpp>
#include <map>
#include <set>

namespace detent::delivery {
namespace d = distribution;
namespace l = local;
namespace {
bool fatal_source_reset(SessionResetReason reason) noexcept {
	return reason == SessionResetReason::Forbidden || reason == SessionResetReason::Unauthorized ||
		   reason == SessionResetReason::IdentityChanged || reason == SessionResetReason::InvalidRequest;
}
bool unavailable_reset(SessionResetReason reason) noexcept {
	return reason == SessionResetReason::NotLeader || reason == SessionResetReason::Unavailable ||
		   reason == SessionResetReason::SourceBehind;
}
struct ConstructionControl {
	std::atomic<bool> stop{false};
	std::atomic<std::uint64_t> steps{0};
	void step() {
		if (stop.load(std::memory_order_acquire))
			throw asio::system_error(asio::error::operation_aborted);
		steps.fetch_add(1, std::memory_order_relaxed);
	}
};
// One task on the worker: the owner waits for `ready`, then reads the result or the failure.
struct WorkerResult : d::RetirementNode {
	ConstructionControl control;
	std::exception_ptr failure;
	std::atomic<bool> ready{false};
};
struct RecoveryResult : WorkerResult {
	std::optional<l::RecoveredGeneration> generation;
};
struct BuildResult : WorkerResult {
	std::optional<l::detail::PagedPlan> plan;
	bool unchanged = false, repair = false, compaction = false;
};
struct ScrubResult : d::RetirementNode {
	explicit ScrubResult(const CoordinatorOptions &options)
		: scrub(options.scrub_bytes_per_second), directory(options.directory) {}
	l::detail::PackScrub scrub;
	const std::filesystem::path directory;
	std::string name;
	d::RetirementHandle<l::detail::LeasedPagedRoot> root;
	std::exception_ptr failure;
	std::atomic<bool> ready{true};
};
// The frames of one response (W6), each held as the paged builders read it; none is visible until
// next returns Frames and the coordinator publishes them in one cycle.
class Response final : public FramePreparer {
  public:
	explicit Response(const d::Limits &limits) : limits_(limits) {}
	class Frame final : public Prepared {
	  public:
		Frame(Response &owner, std::string name, std::unique_ptr<d::FrameInput> input)
			: owner_(owner), name_(std::move(name)), input_(std::move(input)) {}
		void piece(d::PieceKind kind, std::uint64_t first, std::uint64_t last) override {
			if (input_)
				input_->piece(kind, first, last);
		}
		void record(std::string_view key, std::uint64_t revision, const KnobDef *value) override {
			if (input_)
				input_->record(key, revision, value);
		}
		// The input's storage admits its bytes to the heap as it grows: nothing to charge twice.
		std::size_t bytes() const noexcept override { return 0; }
		void publish() override {
			if (input_)
				owner_.frames.push_back(std::move(input_));
			else
				owner_.dropped.push_back(std::move(name_));
		}

	  private:
		Response &owner_;
		std::string name_;
		std::unique_ptr<d::FrameInput> input_;
	};
	std::unique_ptr<Prepared> prepare(std::string_view name, d::FrameKind kind, std::uint64_t after,
									  const d::NamespaceCursor &to) override {
		if (kind == d::FrameKind::Dropped)
			return std::make_unique<Frame>(*this, std::string(name), nullptr);
		return std::make_unique<Frame>(*this, std::string(name),
									   std::make_unique<d::FrameInput>(name, kind, after, to, d::Profile{}, limits_));
	}
	void directory(const d::DirectoryUpdate &update) override { directory_update = update; }
	std::vector<std::unique_ptr<d::FrameInput>> frames;
	std::vector<std::string> dropped;
	std::optional<d::DirectoryUpdate> directory_update;

  private:
	d::Limits limits_;
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
	std::uint64_t wakes = 0;
	std::shared_ptr<detail::CoordinatorAccess::PreparedProbePause> prepared_probe_pause, scrub_pause;
	// Per followed namespace (C1): its recovered base, and whether a full copy is asked for it (D10).
	struct Followed {
		d::RetirementHandle<RecoveryResult> base;
		bool repair = false;
	};
	std::map<std::string, Followed, std::less<>> followed;
	bool retains_candidate = false;
	bool compaction_requested = false, fail_compaction_admission = false;
	std::uint64_t completed_compactions = 0;
	std::chrono::steady_clock::time_point next_compaction_check{};
	std::string compaction_cursor;
	task::cancel_source network_cancel;
	std::optional<asio::steady_timer> candidate_wake, disk_wake, scrub_wake;
	d::RetirementHandle<ScrubResult> scrub_result;
	std::uint64_t cancellation = 0, candidate_cancellation = 0;
	bool closing = false, running = false, settled = false;
	bool repair_woke = false;
	Impl(CoordinatorOptions config, std::shared_ptr<l::DiskStore> disk, std::shared_ptr<NativeDriver> completion,
		 d::RetirementDomain &retirement, asio::thread_pool &pool)
		: options(std::move(config)),
		  sources(options.authority_endpoints, options.source.endpoint, options.quorum_refresh,
				  options.reconnect_seed ^ UINT64_C(0x6a09e667f3bcc909)),
		  store(std::move(disk)), driver(std::move(completion)), domain(retirement), worker(pool) {
		for (const auto &name : options.namespaces)
			followed.try_emplace(name);
	}
	bool cancelled() const noexcept { return closing || candidate_cancellation != cancellation; }
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
	void fence(std::exception_ptr failure) noexcept {
		if (!terminal_fenced) {
			terminal_fenced = true;
			terminal_failure = failure;
			++diagnostic.failures;
		}
		diagnostic.error = terminal_failure;
		diagnostic.phase = CoordinatorPhase::Fenced;
		close();
	}
	void fail(std::exception_ptr failure) noexcept {
		++diagnostic.failures;
		diagnostic.error = failure;
	}
	// Asks a full copy of `name` at the next build ordinal (D10.3); a network wait in progress ends.
	void request_repair(const std::string &name, bool wake) noexcept {
		auto &state = followed[name];
		if (state.repair)
			return;
		state.repair = true;
		state.base = {};
		++diagnostic.repair.requested;
		diagnostic.repair.pending = true;
		if (wake && diagnostic.phase == CoordinatorPhase::Receiving) {
			repair_woke = true;
			network_cancel.request();
		}
		notify_candidate();
	}
	std::chrono::milliseconds next_reconnect_delay(ReconnectBackoff &backoff) noexcept {
		reconnect_observation.delay = backoff.next();
		++reconnect_observation.count;
		return reconnect_observation.delay;
	}
	// Waits out `duration`: disk progress does not end it; a close, a cancelled update or a wake does.
	task::task<void> pause(std::chrono::milliseconds duration) {
		const auto expires = std::chrono::steady_clock::now() + duration;
		const auto epoch = cancellation, woken = wakes;
		do
			co_await wait_candidate(expires);
		while (!closing && cancellation == epoch && wakes == woken && std::chrono::steady_clock::now() < expires);
	}
	// Ends a pause so the loop asks again now.
	void wake() noexcept {
		++wakes;
		notify_candidate();
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
		disk_wake->expires_at(store->collection_retry_at());
		const asio::error_code error = co_await task::await_asio<void(asio::error_code)>(
			[&](auto handler) { disk_wake->async_wait(std::move(handler)); });
		if (error && error != asio::error::operation_aborted)
			throw asio::system_error(error);
	}
	// Waits for a worker task; scheduler failure never abandons accepted worker work.
	task::task<void> settle(WorkerResult &result, bool construction_task) {
		if (construction_task)
			construction = &result.control;
		diagnostic.worker_pending = true;
		try {
			if (fail_worker_wait) {
				fail_worker_wait = false;
				throw std::bad_alloc();
			}
			while (!result.ready.load(std::memory_order_acquire))
				co_await pause(std::chrono::milliseconds(1));
			if (std::exchange(fail_settled_worker_wait, false))
				throw std::bad_alloc();
		} catch (...) {
			if (!result.ready.load(std::memory_order_acquire))
				std::terminate();
			if (construction_task)
				settle_construction(result.control);
			diagnostic.worker_pending = false;
			if (result.failure)
				std::rethrow_exception(result.failure);
			throw;
		}
		if (construction_task)
			settle_construction(result.control);
		diagnostic.worker_pending = false;
		if (result.failure)
			std::rethrow_exception(result.failure);
	}
	// Recovers namespace `name`'s selected generation on the worker, through the store's admission
	// when a publication of it awaits confirmation.
	task::task<d::RetirementHandle<RecoveryResult>> recover(const std::string &name) {
		auto result = domain.make<RecoveryResult>();
		auto context = l::detail::ManagedRecovery::capture(*store, name);
		const auto settlement = context;
		asio::post(worker, [result, directory = options.directory, context = std::move(context),
							probe = std::exchange(prepared_probe_pause, {}),
							injected = std::exchange(injected_worker_failure, {})]() mutable {
			try {
				if (injected)
					std::rethrow_exception(injected);
				result->generation.emplace(l::detail::ManagedRecovery::recover(directory, std::move(context)));
				if (probe) {
					probe->reached.store(true, std::memory_order_release);
					probe->resume.wait();
				}
			} catch (...) {
				result->failure = std::current_exception();
			}
			result->ready.store(true, std::memory_order_release);
		});
		try {
			co_await settle(*result, false);
		} catch (...) {
			if (result->failure && settlement.paged.admission)
				l::detail::ManagedRecovery::settle_failure(*store, settlement, result->failure);
			else if (result->failure && damaged(result->failure)) {
				// A selected entry that does not recover fails alone; a fresh full copy replaces it (D136).
				store->fail(name);
				request_repair(name, false);
			}
			throw;
		}
		co_return result;
	}
	// Whether a recovery failure is the entry's bytes, not memory or disk pressure.
	static bool damaged(std::exception_ptr failure) noexcept {
		try {
			std::rethrow_exception(failure);
		} catch (const d::StoragePressure &) {
		} catch (const l::DiskPressure &) {
		} catch (const std::bad_alloc &) {
		} catch (...) {
			return true;
		}
		return false;
	}
	// The recovered base of `name`, recovered now when not held or when the selection moved.
	task::task<const l::RecoveredGeneration *> base_of(const std::string &name) {
		const auto selection = store->selection();
		const auto *selected = selection.find(name);
		if (!selected)
			co_return nullptr;
		auto &state = followed[name];
		if (!state.base || !state.base->generation || state.base->generation->descriptor() != *selected)
			state.base = co_await recover(name);
		co_return &*state.base->generation;
	}
	template <class Work> task::task<d::RetirementHandle<BuildResult>> build(Work work) {
		auto result = domain.make<BuildResult>();
		asio::post(worker, [result, work = std::move(work),
							injected = std::exchange(injected_worker_failure, {})]() mutable {
			try {
				if (injected)
					std::rethrow_exception(injected);
				work(*result);
			} catch (...) {
				result->failure = std::current_exception();
			}
			result->ready.store(true, std::memory_order_release);
		});
		retains_candidate = true;
		try {
			co_await settle(*result, true);
		} catch (...) {
			retains_candidate = false;
			throw;
		}
		retains_candidate = false;
		co_return result;
	}
	template <class Builder> static void drive(Builder &builder, ConstructionControl &control) {
		while (!builder.done()) {
			control.step();
			l::ValidationBudget budget;
			builder.step(budget);
		}
	}
	// One namespace's frame as a plan: a full copy builds fresh (or on its base, at the same incarnation);
	// entries and blocks apply to the namespace's base.
	task::task<d::RetirementHandle<BuildResult>> build_frame(d::FrameInput &input) {
		const auto name = input.name();
		const auto target = input.target();
		const auto selection = store->selection();
		const auto *selected = selection.find(name);
		const auto at = selected ? std::optional(selected->cursor) : std::nullopt;
		auto factory = domain.factory();
		auto heap = domain.storage_account();
		auto limits = options.view_limits;
		limits.retirement_domain = nullptr;
		const bool repair = followed[name].repair;
		if (options.cluster != d::Identity{} && target.cluster != options.cluster)
			throw SourceTrustError("namespace frame of another cluster");
		if (input.kind() == d::FrameKind::FullCopy) {
			std::optional<l::RecoveredGeneration> actual;
			if (at && at->same_identity(target))
				actual = *co_await base_of(name);
			const auto directory = store->directory_identity();
			co_return co_await build([snapshot = std::shared_ptr<const d::ValidatedSnapshot>(input.snapshot()),
									  actual = std::move(actual), directory, factory, heap, limits,
									  repair](BuildResult &result) mutable {
				l::detail::PagedSnapshotPlanBuilder candidate(std::move(snapshot), std::move(actual), directory, factory,
															  heap, limits, repair);
				drive(candidate, result.control);
				if (candidate.unchanged()) {
					candidate.take_unchanged_proof();
					result.unchanged = true;
				} else {
					result.plan.emplace(candidate.take());
					result.repair = result.plan->intent() == l::detail::PagedPlanIntent::Repair;
				}
			});
		}
		if (!at || *at != d::NamespaceCursor{target.cluster, target.incarnation, input.after()})
			throw std::invalid_argument("namespace frame of " + name + " after " + std::to_string(input.after()) +
										" does not start at the store's cursor " +
										(at ? std::to_string(at->position) + (at->same_identity(target) ? "" : " of another incarnation")
											: std::string("(none)")));
		const auto *base = co_await base_of(name);
		if (!base)
			throw std::invalid_argument("namespace delta requires a base");
		const auto bounds = l::detail::paged_limits(limits, l::paged::root_bytes);
		co_return co_await build([base = *base, delta = std::shared_ptr<const d::ValidatedDelta>(input.delta()),
								  factory, heap, bounds](BuildResult &result) mutable {
			l::detail::PagedDeltaPlanBuilder candidate(base, std::move(delta), factory, heap, bounds);
			drive(candidate, result.control);
			result.plan.emplace(candidate.take());
		});
	}
	// Publishes one cycle and confirms each published namespace's recovery.
	task::task<bool> publish(std::vector<l::NamespacePublication> cycle, std::vector<std::string> dropped,
							 std::optional<d::DirectoryCursor> directory) {
		diagnostic.phase = CoordinatorPhase::WaitingPublication;
		const auto expires = std::chrono::steady_clock::now() + options.source.timeout;
		for (;;) {
			if (cancelled())
				co_return false;
			if (std::chrono::steady_clock::now() >= expires)
				throw std::runtime_error("managed publication admission deadline");
			if (store->status() != l::StoreStatus::Ready)
				throw std::runtime_error("managed store is not ready");
			if (!store->publication_ready()) {
				co_await pause(std::chrono::milliseconds(1));
				continue;
			}
			try {
				store->begin(std::move(cycle), std::move(dropped), directory);
				break;
			} catch (const l::DiskPressure &) {
			} catch (const d::StoragePressure &) {
			}
			store->request_collection();
			notify_disk();
			co_await pause(options.retry_interval);
		}
		diagnostic.phase = CoordinatorPhase::Publishing;
		notify_disk();
		while (store->publication_status() == l::DiskPublicationStatus::Preparing)
			co_await wait_candidate(std::chrono::steady_clock::time_point::max());
		if (store->publication_status() != l::DiskPublicationStatus::Published) {
			if (auto error = store->publication_error())
				std::rethrow_exception(error);
			co_return false;
		}
		diagnostic.phase = CoordinatorPhase::Reconciling;
		diagnostic.recovery_started_at = std::chrono::steady_clock::now();
		for (const auto &name : store->pending()) {
			auto recovered = co_await recover(name);
			store->confirm_recovered(name, *recovered->generation);
			followed[name].base = std::move(recovered);
		}
		notify_disk();
		co_return true;
	}
	// The names followed now: the configured ones, and with `all` every name the store holds.
	Following following() {
		const auto selection = store->selection();
		std::set<std::string, std::less<>> names(options.namespaces.begin(), options.namespaces.end());
		if (options.all)
			for (const auto &entry : selection.namespaces)
				names.insert(entry.name);
		Following out;
		for (const auto &name : names) {
			FollowedNamespace entry{name, {}};
			const auto found = followed.find(name);
			if (const auto *selected = selection.find(name); selected && (found == followed.end() || !found->second.repair))
				entry.cursor = selected->cursor;
			out.namespaces.push_back(std::move(entry));
		}
		if (options.all)
			out.all = selection.directory;
		return out;
	}
	void refresh_status() {
		const auto selection = store->selection();
		diagnostic.namespaces.clear();
		std::set<std::string, std::less<>> names(options.namespaces.begin(), options.namespaces.end());
		for (const auto &entry : selection.namespaces)
			names.insert(entry.name);
		bool repair = false;
		for (const auto &name : names) {
			NamespaceStatus status{name, {}, false};
			if (const auto *selected = selection.find(name))
				status.cursor = selected->cursor;
			if (const auto found = followed.find(name); found != followed.end())
				status.repair = found->second.repair;
			repair = repair || status.repair;
			diagnostic.namespaces.push_back(std::move(status));
		}
		diagnostic.repair.pending = repair;
		if (options.all)
			diagnostic.directory = selection.directory;
		// A restart is ready from its store when every configured namespace is published (C4).
		if (!diagnostic.ready && !options.all &&
			std::all_of(options.namespaces.begin(), options.namespaces.end(),
						[&](const auto &name) { return selection.find(name) != nullptr; }))
			diagnostic.ready = true;
	}
	// Applies one response: each frame built in name order, then one cycle.
	task::task<void> apply(Response &response) {
		diagnostic.phase = CoordinatorPhase::Building;
		std::vector<l::NamespacePublication> cycle;
		std::vector<std::string> repaired;
		for (auto &input : response.frames) {
			if (cancelled())
				throw asio::system_error(asio::error::operation_aborted);
			const auto name = input->name();
			d::RetirementHandle<BuildResult> built;
			try {
				built = co_await build_frame(*input);
			} catch (const l::detail::ReadSourceFailure &) {
				// A page of the base failed first touch: the namespace is asked as a full copy.
				request_repair(name, false);
				throw;
			}
			input.reset();
			if (built->unchanged)
				continue;
			if (built->repair)
				repaired.push_back(name);
			cycle.push_back({name, std::make_unique<l::detail::PagedPlan>(std::move(*built->plan))});
		}
		std::vector<std::string> dropped;
		const auto selection = store->selection();
		for (auto &name : response.dropped)
			if (selection.find(name))
				dropped.push_back(std::move(name));
		if (!options.all)
			for (const auto &entry : selection.namespaces)
				if (!followed.contains(entry.name) &&
					std::find(dropped.begin(), dropped.end(), entry.name) == dropped.end())
					dropped.push_back(entry.name);
		std::sort(dropped.begin(), dropped.end());
		std::optional<d::DirectoryCursor> directory;
		if (response.directory_update)
			directory = response.directory_update->to;
		if (cycle.empty() && dropped.empty() && !directory)
			co_return;
		if (co_await publish(std::move(cycle), dropped, directory)) {
			for (const auto &name : dropped)
				followed.erase(name);
			for (const auto &name : repaired) {
				followed[name].repair = false;
				++diagnostic.repair.completed;
			}
			if (!repaired.empty())
				store->request_collection();
			++diagnostic.responses;
		}
	}
	task::task<void> compact() {
		const auto now = std::chrono::steady_clock::now();
		if (!compaction_requested &&
			(options.compaction_interval == std::chrono::milliseconds::zero() || now < next_compaction_check))
			co_return;
		const bool force = std::exchange(compaction_requested, false);
		next_compaction_check = now + options.compaction_interval;
		// One namespace per check, in name order.
		const auto selection = store->selection();
		if (selection.namespaces.empty())
			co_return;
		auto next = std::upper_bound(selection.namespaces.begin(), selection.namespaces.end(), compaction_cursor,
									 [](const std::string &key, const auto &entry) { return key < entry.name; });
		if (next == selection.namespaces.end())
			next = selection.namespaces.begin();
		const auto name = next->name;
		compaction_cursor = name;
		if (followed[name].repair)
			co_return;
		diagnostic.phase = CoordinatorPhase::Building;
		d::RetirementHandle<BuildResult> built;
		try {
			const auto *base = co_await base_of(name);
			if (!base)
				co_return;
			auto factory = domain.factory();
			auto heap = domain.storage_account();
			auto limits = options.view_limits;
			limits.retirement_domain = nullptr;
			built = co_await build([base = *base, force, factory, heap, limits](BuildResult &result) mutable {
				result.compaction = true;
				std::array<l::SegmentId, 8> chosen{};
				std::size_t count = 0;
				const auto &root = l::detail::ScrubSource::root(base);
				const auto inputs = root->registry()->inputs();
				for (std::size_t i = 0; i < inputs.size() && count < chosen.size(); ++i) {
					const auto live = root->registry()->reachability(i);
					if (live.pages && (force || live.bytes < inputs[i].ownership.length / 2)) {
						chosen[count++] = inputs[i].ownership.segment;
						if (force)
							break;
					}
				}
				if (!count)
					return;
				const auto bounds = l::detail::paged_limits(limits, l::paged::root_bytes);
				l::detail::PagedCompactionBuilder candidate(base, {chosen.data(), count}, factory, heap,
															l::detail::CompactionLimits{}, bounds);
				drive(candidate, result.control);
				result.plan.emplace(candidate.take());
			});
		} catch (const d::StoragePressure &) {
			fail(std::current_exception());
			co_return;
		} catch (const std::bad_alloc &) {
			fail(std::current_exception());
			co_return;
		} catch (const std::length_error &) {
			fail(std::current_exception());
			co_return;
		}
		if (!built->plan)
			co_return;
		if (std::exchange(fail_compaction_admission, false)) {
			fail(std::make_exception_ptr(d::StoragePressure("injected compaction admission pressure")));
			co_return;
		}
		std::vector<l::NamespacePublication> cycle;
		cycle.push_back({name, std::make_unique<l::detail::PagedPlan>(std::move(*built->plan))});
		if (co_await publish(std::move(cycle), {}, {}))
			++completed_compactions;
	}
	task::task<void> scrub_loop() {
		try {
			co_await scrub_loop_body();
		} catch (...) {
			std::terminate();
		}
	}
	// One pass at a time over each namespace's selected generation, in name order (D10.2).
	task::task<void> scrub_loop_body() {
		if (!options.scrub_bytes_per_second)
			co_return;
		constexpr auto tick = std::chrono::milliseconds(20);
		constexpr std::uint64_t per_tick = 2 * 1024 * 1024;
		try {
			scrub_result = domain.make<ScrubResult>(options);
		} catch (...) {
			fence(std::current_exception());
			co_return;
		}
		std::uint64_t reported_passes = 0;
		while (!closing) {
			const auto now = std::chrono::steady_clock::now();
			auto result = scrub_result;
			if (diagnostic.initialized && !result->scrub.running()) {
				// The next namespace whose base the coordinator holds.
				std::string next;
				for (const auto &[name, state] : followed)
					if (state.base && state.base->generation && name > result->name) {
						next = name;
						break;
					}
				if (next.empty())
					for (const auto &[name, state] : followed)
						if (state.base && state.base->generation) {
							next = name;
							break;
						}
				if (!next.empty()) {
					auto root = l::detail::ScrubSource::root(*followed[next].base->generation);
					if (root) {
						result->name = next;
						result->root = std::move(root);
						result->failure = {};
						result->ready.store(false, std::memory_order_relaxed);
						asio::post(worker, [result, now] {
							try {
								result->scrub.begin(result->root->registry()->inputs(), now, result->directory);
							} catch (...) {
								result->failure = std::current_exception();
							}
							result->ready.store(true, std::memory_order_release);
						});
						co_await wait_scrub(*result);
						reported_passes = result->scrub.passes();
					}
				}
			}
			if (result->root && result->scrub.running() && !closing) {
				result->failure = {};
				result->ready.store(false, std::memory_order_relaxed);
				asio::post(worker, [result, now, paused = scrub_pause] {
					try {
						const auto passes = result->scrub.passes();
						result->scrub.step(now, per_tick);
						if (paused && result->scrub.passes() != passes) {
							paused->reached.store(true, std::memory_order_release);
							paused->resume.wait();
						}
					} catch (...) {
						result->failure = std::current_exception();
					}
					result->ready.store(true, std::memory_order_release);
				});
				co_await wait_scrub(*result);
				if (scrub_pause && scrub_pause->reached.load(std::memory_order_acquire))
					scrub_pause.reset();
				if (closing)
					break;
				auto &scrubber = result->scrub;
				diagnostic.scrub.passes = scrubber.passes();
				diagnostic.scrub.bytes_hashed = scrubber.bytes_hashed();
				if (result->failure)
					fail(result->failure);
				// One report per completed pass: the packs that pass found.
				if (scrubber.passes() != reported_passes) {
					reported_passes = scrubber.passes();
					diagnostic.scrub.damaged_packs = scrubber.failures().size();
					diagnostic.scrub.source_failure = !scrubber.failures().empty();
					if (diagnostic.scrub.source_failure) {
						fail(std::make_exception_ptr(std::runtime_error(
							"scrub: a pack of " + result->name + "'s generation differs from its owner record")));
						request_repair(result->name, true);
					}
					result->root = {};
				}
			}
			scrub_wake->expires_after(tick);
			const asio::error_code error = co_await task::await_asio<void(asio::error_code)>(
				[&](auto handler) { scrub_wake->async_wait(std::move(handler)); });
			if (error && error != asio::error::operation_aborted)
				throw asio::system_error(error);
		}
		scrub_result.reset();
	}
	task::task<void> wait_scrub(ScrubResult &result) {
		while (!result.ready.load(std::memory_order_acquire)) {
			scrub_wake->expires_after(std::chrono::milliseconds(1));
			const auto error = co_await task::await_asio<void(asio::error_code)>(
				[&](auto handler) { scrub_wake->async_wait(std::move(handler)); });
			if (error && error != asio::error::operation_aborted)
				throw asio::system_error(error);
		}
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
			store->advance();
			notify_candidate();
			if (auto work = store->work()) {
				// The worker runs the store's disk work; the owner settles it once the work is done.
				auto done = std::make_shared<std::atomic<bool>>(false);
				asio::post(worker, [work, done] {
					work->run();
					done->store(true, std::memory_order_release);
				});
				while (!done->load(std::memory_order_acquire)) {
					disk_wake->expires_after(std::chrono::milliseconds(1));
					const asio::error_code error = co_await task::await_asio<void(asio::error_code)>(
						[&](auto handler) { disk_wake->async_wait(std::move(handler)); });
					if (error && error != asio::error::operation_aborted)
						throw asio::system_error(error);
				}
				store->advance();
				if (store->error() || store->publication_status() == l::DiskPublicationStatus::Fenced)
					fence(store->error() ? store->error() : store->publication_error());
				notify_candidate();
				if (++burst == 16) {
					burst = 0;
					co_await task::yield();
				}
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
		++cancellation;
		network_cancel.request();
		notify_candidate();
		notify_disk();
		if (scrub_wake)
			scrub_wake->cancel();
		if (store)
			store->close();
	}
	task::task<void> candidate_loop();
};

namespace {
// A malformed close from a pooled source fails closed (`strict`), as any malformed answer does.
task::task<void> release_session(ClientOptions options, NamespaceSession session, bool strict) {
	options.timeout = std::min(options.timeout, std::chrono::milliseconds(1000));
	try {
		co_await close_namespace_session(std::move(options), session);
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
	NamespaceSession session;
	auto opening_endpoint = options.source.endpoint;
	auto session_source = [&] {
		auto source = options.source;
		source.endpoint = opening_endpoint;
		return source;
	};
	bool revalidating = false;
	ReconnectBackoff reconnect(options.retry_interval, options.reconnect_maximum, options.reconnect_seed);
	std::exception_ptr fatal;
	try {
		if (!options.source.connections)
			options.source.connections =
				make_connection_cache(options.source.scheduler->executor(), 1, options.source.limits.storage_account);
		while (!closing && store->status() == l::StoreStatus::Starting)
			co_await wait_candidate(std::chrono::steady_clock::time_point::max());
		if (!closing) {
			if (store->status() != l::StoreStatus::Ready)
				throw std::runtime_error("managed store is not recoverable");
			diagnostic.phase = CoordinatorPhase::Recovering;
			// An entry that did not verify at startup is fetched again as a full copy (D10).
			for (const auto &name : store->failed())
				request_repair(name, false);
			diagnostic.initialized = true;
			refresh_status();
		}
		while (!closing) {
			candidate_cancellation = cancellation;
			network_cancel = task::cancel_source{};
			bool network_attempt = false, unavailable = false, round_complete = true, idle = false;
			std::exception_ptr failure;
			try {
				co_await compact();
				if (cancelled())
					throw asio::system_error(asio::error::operation_aborted);
				if (session.valid() && sources.refresh_due(std::chrono::steady_clock::now())) {
					revalidating = true;
					co_await release_session(session_source(), session, sources.pooled());
					session = {};
				}
				auto request = options.source;
				request.endpoint = sources.endpoint();
				diagnostic.phase = CoordinatorPhase::Receiving;
				if (!session.valid()) {
					network_attempt = true;
					session = co_await open_namespace_session(request, following(), network_cancel.token());
					opening_endpoint = request.endpoint;
					const auto opened = std::chrono::steady_clock::now();
					sources.opened(opened);
					if (sources.pooled())
						diagnostic.last_quorum_open = opened;
					revalidating = false;
					if (std::exchange(close_after_session_open, false))
						close();
				}
				if (cancelled())
					throw asio::system_error(asio::error::operation_aborted);
				const auto wait = sources.wait(std::chrono::steady_clock::now(),
											   std::min({session.maximum_wait, std::chrono::milliseconds(30000),
														 request.timeout / 2}));
				Response response(request.limits);
				network_attempt = true;
				const auto outcome =
					co_await next_namespace_session(request, session, following(), wait, response, network_cancel.token());
				network_attempt = false;
				sources.successful();
				if (outcome == FramesOutcome::Idle) {
					idle = true;
					++diagnostic.idle;
				} else
					co_await apply(response);
				if (!cancelled()) {
					diagnostic.ready = true;
					reconnect.reset();
				}
				if (idle && wait == std::chrono::milliseconds::zero())
					co_await pause(options.retry_interval);
			} catch (const d::StoragePressure &) {
				failure = std::current_exception();
			} catch (const std::bad_alloc &) {
				failure = std::current_exception();
			} catch (...) {
				failure = std::current_exception();
			}
			if (!closing)
				refresh_status();
			// The network wait that a repair request ended is not a failure.
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
			if (failure && !cancelled() && !woken)
				fail(failure);
			if (failure || cancelled()) {
				if (session.valid())
					co_await release_session(session_source(), session, sources.pooled());
				session = {};
			}
			if (failure) {
				try {
					std::rethrow_exception(failure);
				} catch (const SourceTrustError &) {
					throw;
				} catch (const l::GenerationUnavailable &) {
					throw; // a selected entry the store can no longer recover: the store is gone
				} catch (const SessionResetError &error) {
					if (fatal_source_reset(error.reason()))
						throw;
					unavailable = unavailable_reset(error.reason());
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
			if (store->status() != l::StoreStatus::Ready)
				throw std::runtime_error("managed selection is not recoverable");
			if (failure && !woken) {
				if (sources.pooled() && unavailable && !round_complete)
					co_await task::yield();
				else
					co_await pause(next_reconnect_delay(reconnect));
			}
		}
	} catch (...) {
		fatal = std::current_exception();
	}
	try {
		if (session.valid())
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
	followed.clear();
}

Coordinator::Coordinator(CoordinatorOptions options, std::shared_ptr<l::DiskStore> store,
						 std::shared_ptr<NativeDriver> driver, d::RetirementDomain &domain,
						 asio::thread_pool &worker) {
	const bool ordered = std::adjacent_find(options.namespaces.begin(), options.namespaces.end(),
											[](const auto &a, const auto &b) { return !(a < b); }) ==
						 options.namespaces.end();
	if (!ordered || (options.namespaces.empty() && !options.all) ||
		options.compaction_interval < std::chrono::milliseconds::zero() ||
		options.compaction_interval > std::chrono::hours(24) ||
		(options.compaction_interval != std::chrono::milliseconds::zero() &&
		 options.compaction_interval < std::chrono::seconds(1)) ||
		!driver || options.directory.empty() || options.retry_interval <= std::chrono::milliseconds::zero() ||
		options.retry_interval > std::chrono::hours(24) ||
		options.reconnect_maximum < std::chrono::milliseconds::zero() ||
		options.reconnect_maximum > std::chrono::hours(24) ||
		(options.reconnect_maximum != std::chrono::milliseconds::zero() &&
		 options.reconnect_maximum < options.retry_interval) ||
		(store && store->storage_account() != domain.storage_account()))
		throw std::invalid_argument("invalid managed coordinator configuration");
	options.source.limits.storage_account = domain.storage_account();
	options.view_limits.retirement_domain = &domain;
	impl_ = d::make_storage_owner<Impl>(domain.storage_account(), std::malloc, std::free, std::move(options), store,
										std::move(driver), domain, worker);
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
	if (!store || impl_->store || impl_->running || store->storage_account() != impl_->domain.storage_account())
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
	return coordinator.impl_->retains_candidate;
}
void detail::CoordinatorAccess::pause_next_prepared_probe(Coordinator &coordinator,
														std::shared_ptr<PreparedProbePause> pause) {
	if (!pause || !pause->resume.valid() || coordinator.impl_->prepared_probe_pause)
		throw std::invalid_argument("invalid prepared probe pause");
	coordinator.impl_->prepared_probe_pause = std::move(pause);
}
void detail::CoordinatorAccess::pause_next_scrub(Coordinator &coordinator, std::shared_ptr<PreparedProbePause> pause) {
	if (!pause || !pause->resume.valid() || coordinator.impl_->scrub_pause)
		throw std::invalid_argument("invalid scrub pause");
	coordinator.impl_->scrub_pause = std::move(pause);
}
void detail::CoordinatorAccess::fail_next_maintenance_recovery(Coordinator &coordinator, std::exception_ptr failure) {
	coordinator.impl_->injected_maintenance_failure = failure;
	coordinator.impl_->injected_worker_failure = std::move(failure);
}
void detail::CoordinatorAccess::fail_next_compaction_admission(Coordinator &coordinator) {
	coordinator.impl_->fail_compaction_admission = true;
}
void detail::CoordinatorAccess::request_compaction(Coordinator &coordinator) {
	coordinator.impl_->compaction_requested = true;
	coordinator.impl_->wake();
}
void detail::CoordinatorAccess::request_repair(Coordinator &coordinator, std::string_view name) {
	coordinator.impl_->request_repair(std::string(name), true);
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
bool Coordinator::drained() const noexcept { return impl_->settled && impl_->store && impl_->store->drained(); }
CoordinatorStatus Coordinator::status() const noexcept {
	auto status = impl_->diagnostic;
	status.hash = hash_stats();
	if (impl_->store) {
		status.selected = impl_->store->selection();
		status.sync = impl_->store->sync_stats();
	}
	return status;
}
std::shared_ptr<Coordinator> make_coordinator(CoordinatorOptions options, std::shared_ptr<l::DiskStore> store,
											  std::shared_ptr<NativeDriver> driver, d::RetirementDomain &domain,
											  asio::thread_pool &worker) {
	auto account = domain.storage_account();
	return std::allocate_shared<Coordinator>(d::StorageAllocator<Coordinator>(account), std::move(options),
											 std::move(store), std::move(driver), domain, worker);
}
std::shared_ptr<Coordinator> prepare_coordinator(CoordinatorOptions options, std::shared_ptr<NativeDriver> driver,
												 d::RetirementDomain &domain, asio::thread_pool &worker) {
	return make_coordinator(std::move(options), {}, std::move(driver), domain, worker);
}
} // namespace detent::delivery
