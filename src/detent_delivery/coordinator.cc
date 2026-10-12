#include "detent_delivery/coordinator.hh"
#include "coordinator_internal.hh"
#include "detent_delivery/reconnect_backoff.hh"
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <openssl/rand.h>

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
	l::GenerationId next_id{};
	std::exception_ptr failure;
	std::atomic<bool> ready{false};
};
} // namespace
struct Coordinator::Impl {
	CoordinatorOptions options;
	std::shared_ptr<l::DiskStore> store;
	std::shared_ptr<NativeDriver> driver;
	d::RetirementDomain &domain;
	asio::thread_pool &worker;
	CoordinatorStatus diagnostic;
	std::exception_ptr terminal_failure;
	bool terminal_fenced = false;
	bool fail_worker_wait = false, close_after_session_open = false;
	d::RetirementHandle<RecoveryResult> base;
	d::StorageOwner<l::ImageBuilder> builder;
	asio::cancellation_signal network_cancel;
	std::optional<asio::steady_timer> candidate_wake, disk_wake;
	std::uint64_t cancellation = 0, candidate_cancellation = 0;
	bool closing = false, running = false, settled = false, publication_started = false;
	std::chrono::steady_clock::time_point publication_retry{};
	Impl(CoordinatorOptions config, std::shared_ptr<l::DiskStore> disk,
		 std::shared_ptr<NativeDriver> completion, d::RetirementDomain &retirement, asio::thread_pool &pool)
		: options(std::move(config)), store(std::move(disk)), driver(std::move(completion)),
		  domain(retirement), worker(pool), base(domain.make<RecoveryResult>(options)) {
		if (options.completion_port && !options.completion_port->uses(driver))
			throw std::invalid_argument("coordinator completion port driver mismatch");
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
	asio::awaitable<void> yield() {
		co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
	}
	asio::awaitable<void> pause(std::chrono::milliseconds duration) {
		co_await wait_candidate(std::chrono::steady_clock::now() + duration);
	}
	asio::awaitable<void> wait_candidate(std::chrono::steady_clock::time_point expires) {
		candidate_wake->expires_at(expires);
		asio::error_code error;
		co_await candidate_wake->async_wait(asio::redirect_error(asio::use_awaitable, error));
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
	asio::awaitable<void> wait_disk() {
		auto expires = std::chrono::steady_clock::time_point::max();
		if (diagnostic.phase == CoordinatorPhase::WaitingPublication && !cancelled() &&
			publication_retry > std::chrono::steady_clock::now())
			expires = publication_retry;
		disk_wake->expires_at(expires);
		asio::error_code error;
		co_await disk_wake->async_wait(asio::redirect_error(asio::use_awaitable, error));
		if (error && error != asio::error::operation_aborted)
			throw asio::system_error(error);
	}
	asio::awaitable<d::RetirementHandle<RecoveryResult>> recover(bool selected) {
		auto result = base;
		result->failure = {};
		result->ready.store(false, std::memory_order_relaxed);
		// One candidate coroutine admits at most one configuration-worker task.
		// The task retains its result through authentic worker completion even
		// when request interest has been cancelled on the owner executor.
		asio::post(worker, [result, selected] {
			try {
				if (selected) {
					result->generation.reset();
					result->generation.emplace(l::recover_generation(result->directory, result->limits));
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
		} catch (...) {
			// Scheduler failure cannot manufacture authentic worker settlement.
			if (!result->ready.load(std::memory_order_acquire))
				std::terminate();
			diagnostic.worker_pending = false;
			throw;
		}
		diagnostic.worker_pending = false;
		if (result->failure)
			std::rethrow_exception(result->failure);
		co_return result;
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
		base = std::move(recovered);
		if (options.publication_sink && base->generation)
			options.publication_sink->selected(*base->generation);
	}
	void offer_publication() {
		if (diagnostic.phase != CoordinatorPhase::WaitingPublication || cancelled() ||
			!store->publication_ready() || std::chrono::steady_clock::now() < publication_retry)
			return;
		const auto actual = store->selection();
		if (actual.knowledge == l::SelectionKnowledge::Known &&
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
	asio::awaitable<void> disk_loop() {
		try {
			co_await disk_loop_body();
		} catch (...) {
			std::terminate();
		}
	}
	asio::awaitable<void> disk_loop_body() {
		unsigned burst = 0;
		while (!store->drained()) {
			// Publication gets the settled boundary before advance resumes GC.
			std::exception_ptr failure;
			try {
				offer_publication();
			} catch (const l::DiskPressure &) {
				store->request_collection();
				publication_retry = std::chrono::steady_clock::now() + options.poll_interval;
			} catch (const d::StoragePressure &) {
				store->request_collection();
				publication_retry = std::chrono::steady_clock::now() + options.poll_interval;
			} catch (const std::bad_alloc &) {
				store->request_collection();
				publication_retry = std::chrono::steady_clock::now() + options.poll_interval;
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
					co_await yield();
				}
			} else if (store->needs_work()) {
				co_await yield();
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
		closing = true;
		if (options.publication_sink)
			options.publication_sink->fence();
		++cancellation;
		network_cancel.emit(asio::cancellation_type::terminal);
		notify_candidate();
		notify_disk();
		if (store)
			store->close();
	}
	asio::awaitable<void> discard(std::optional<ReceivedUpdate> &update) {
		diagnostic.phase = CoordinatorPhase::Discarding;
		if (options.publication_sink) {
			for (;;) {
				d::WorkBudget budget;
				if (options.publication_sink->release_stage(budget))
					break;
				co_await yield();
			}
		}
		if (builder) {
			for (;;) {
				l::ValidationBudget budget;
				if (builder->discard(budget))
					break;
				co_await yield();
			}
			builder.reset();
		}
		if (update) {
			for (;;) {
				d::WorkBudget budget;
				if (update->snapshot ? update->snapshot->discard(budget) : update->delta->discard(budget))
					break;
				co_await yield();
			}
			update.reset();
		}
	}
	asio::awaitable<void> candidate_loop();
};

namespace {
asio::awaitable<void> release_session(ClientOptions options, SessionHandle &session) {
	co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
	options.timeout = std::min(options.timeout, std::chrono::milliseconds(1000));
	try {
		co_await close_session(std::move(options), session);
	} catch (...) {
	}
}
} // namespace
asio::awaitable<void> Coordinator::Impl::candidate_loop() {
	SessionHandle session;
	std::optional<PublicationFeedback> feedback;
	bool force_snapshot = false;
	ReconnectBackoff reconnect(options.poll_interval, options.reconnect_maximum, options.reconnect_seed);
	std::exception_ptr fatal;
	try {
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
			publication_started = false;
			publication_retry = {};
			std::optional<ReceivedUpdate> update;
			std::optional<PublicationFeedback> offered;
			bool reset_session = false, idle = false, offered_snapshot = false;
			std::exception_ptr failure;
			try {
				auto request = options.source;
				const auto actual = store->selection();
				request.base =
					actual.descriptor && actual.descriptor->cursor.same_identity(request.configured)
						? std::optional(actual.descriptor->cursor)
						: std::nullopt;
				diagnostic.phase = CoordinatorPhase::Receiving;
				if (options.delivery_mode == DeliveryMode::Subscription) {
					const auto executor = co_await asio::this_coro::executor;
					if (!session.valid()) {
						session = co_await asio::co_spawn(
							executor, open_session(request, force_snapshot),
							asio::bind_cancellation_slot(network_cancel.slot(), asio::use_awaitable));
						feedback.reset();
						if (options.publication_sink && !cancelled())
							options.publication_sink->authenticated();
						if (std::exchange(close_after_session_open, false))
							close();
					}
					if (cancelled())
						throw asio::system_error(asio::error::operation_aborted);
					const auto wait = std::min(
						{session.maximum_wait(), std::chrono::milliseconds(30000), request.timeout / 2});
					auto event = co_await asio::co_spawn(
						executor, next_session(request, session, feedback, wait),
						asio::bind_cancellation_slot(network_cancel.slot(), asio::use_awaitable));
					if (auto *offer = std::get_if<SessionOffer>(&event)) {
						offered = PublicationFeedback{offer->window, offer->update.target};
						offered_snapshot = offer->update.kind == d::UpdateKind::Snapshot;
						update.emplace(std::move(offer->update));
					} else if (std::holds_alternative<SessionIdle>(event)) {
						idle = true;
						reconnect.reset();
					} else {
						const auto reason = std::get<SessionReset>(event).reason;
						if (reason == SessionResetReason::Forbidden ||
							reason == SessionResetReason::Unauthorized ||
							reason == SessionResetReason::IdentityChanged ||
							reason == SessionResetReason::InvalidRequest)
							throw SessionResetError(reason);
						reset_session = !session.valid();
						force_snapshot = force_snapshot || reason == SessionResetReason::HistoryLost ||
										 reason == SessionResetReason::RepresentationLimit;
					}
					if (!update && !cancelled()) {
						if (reset_session) {
							co_await release_session(options.source, session);
							session = {};
							feedback.reset();
						}
						if (!idle || wait == std::chrono::milliseconds::zero())
							co_await pause(idle ? options.poll_interval : reconnect.next());
						continue;
					}
				} else {
					update.emplace(co_await asio::co_spawn(
						co_await asio::this_coro::executor, receive(std::move(request)),
						asio::bind_cancellation_slot(network_cancel.slot(), asio::use_awaitable)));
				}
				if (!update)
					throw asio::system_error(asio::error::operation_aborted);
				diagnostic.received = update->target;
				diagnostic.validated = update->target;
				if (!cancelled()) {
					if (options.publication_sink) {
						options.publication_sink->begin_stage(*update);
						while (!cancelled()) {
							d::WorkBudget budget;
							if (options.publication_sink->stage_step(budget))
								break;
							co_await yield();
						}
					}
					if (cancelled())
						throw asio::system_error(asio::error::operation_aborted);
					diagnostic.phase = CoordinatorPhase::Building;
					auto account = store->storage_account();
					if (update->snapshot)
						builder = d::make_storage_owner<l::ImageBuilder>(account, std::malloc, std::free,
																		 update->snapshot, base->next_id,
																		 options.view_limits);
					else {
						if (!base->generation || base->generation->descriptor() != actual.descriptor)
							throw std::invalid_argument("managed delta has no exact recovered base");
						builder = d::make_storage_owner<l::ImageBuilder>(account, std::malloc, std::free,
																		 update->delta, *base->generation,
																		 base->next_id, options.view_limits);
					}
					while (!builder->done() && !cancelled()) {
						l::ValidationBudget budget;
						builder->step(budget);
						if (!builder->done())
							co_await yield();
					}
					if (!cancelled()) {
						const auto candidate = builder->descriptor();
						if (options.publication_sink)
							options.publication_sink->bind_candidate(candidate, store->directory_identity());
						if (actual.descriptor && candidate.cursor == actual.descriptor->cursor) {
							if (candidate.length != actual.descriptor->length ||
								candidate.checksum != actual.descriptor->checksum ||
								candidate.profile != actual.descriptor->profile)
								throw std::invalid_argument("same-cursor managed state conflict");
							if (options.publication_sink)
								options.publication_sink->bind_candidate(*actual.descriptor,
																		 store->directory_identity());
							diagnostic.acknowledged = candidate.cursor;
						} else {
							diagnostic.phase = CoordinatorPhase::WaitingPublication;
							notify_disk();
							const auto expires = std::chrono::steady_clock::now() + options.source.timeout;
							while (!cancelled() && !publication_started) {
								if (std::chrono::steady_clock::now() >= expires)
									throw std::runtime_error("managed publication admission deadline");
								co_await wait_candidate(expires);
							}
							while (publication_started &&
								   store->publication_status() == l::DiskPublicationStatus::Preparing)
								co_await wait_candidate(std::chrono::steady_clock::time_point::max());
							if (!cancelled() &&
								store->publication_status() == l::DiskPublicationStatus::Published)
								diagnostic.acknowledged = candidate.cursor;
							else if (!cancelled() && store->publication_error())
								std::rethrow_exception(store->publication_error());
						}
					}
				}
			} catch (const SessionResetError &error) {
				failure = std::current_exception();
				if (options.publication_sink && fatal_source_reset(error.reason()))
					options.publication_sink->fence();
			} catch (...) {
				failure = std::current_exception();
			}
			if (failure && !cancelled()) {
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
					co_await release_session(options.source, session);
				session = {};
				feedback.reset();
			}
			if (failure) {
				try {
					std::rethrow_exception(failure);
				} catch (const SessionResetError &error) {
					const auto reason = error.reason();
					if (reason == SessionResetReason::Forbidden ||
						reason == SessionResetReason::Unauthorized ||
						reason == SessionResetReason::IdentityChanged ||
						reason == SessionResetReason::InvalidRequest)
						throw;
				} catch (...) {
				}
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
			auto recovered = co_await recover(changed);
			if (!closing)
				bind_base(std::move(recovered));
			if (!closing && options.delivery_mode == DeliveryMode::Subscription && session.valid() &&
				offered) {
				if (actual.descriptor && actual.descriptor->cursor == offered->cursor && base->generation &&
					base->generation->descriptor() == actual.descriptor && !failure && !cancelled()) {
					feedback = offered;
					reconnect.reset();
					if (offered_snapshot)
						force_snapshot = false;
				} else {
					co_await release_session(options.source, session);
					session = {};
					feedback.reset();
				}
			}
			if (!closing && (options.delivery_mode == DeliveryMode::Poll || !session.valid()))
				co_await pause(options.delivery_mode == DeliveryMode::Poll ? options.poll_interval
																		   : reconnect.next());
		}
	} catch (...) {
		fatal = std::current_exception();
	}
	if (session.identity() != d::Identity{})
		co_await release_session(options.source, session);
	if (fatal) {
		if (diagnostic.worker_pending)
			std::terminate();
		fence(fatal);
	}
	close();
	if (options.publication_sink)
		options.publication_sink->fence();
	base.reset();
}

Coordinator::Coordinator(CoordinatorOptions options, std::shared_ptr<l::DiskStore> store,
						 std::shared_ptr<NativeDriver> driver, d::RetirementDomain &domain,
						 asio::thread_pool &worker) {
	if (!driver || options.directory.empty() || options.poll_interval <= std::chrono::milliseconds::zero() ||
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
asio::awaitable<void> Coordinator::run() {
	if (impl_->running || !impl_->store)
		throw std::logic_error("managed coordinator already started");
	auto owner = shared_from_this();
	impl_->running = true;
	return run_owned(std::move(owner));
}
asio::awaitable<void> Coordinator::run_owned(std::shared_ptr<Coordinator> owner) {
	try {
		co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
		auto executor = co_await asio::this_coro::executor;
		owner->impl_->candidate_wake.emplace(executor);
		owner->impl_->disk_wake.emplace(executor);
		using namespace asio::experimental::awaitable_operators;
		co_await (owner->impl_->disk_loop() && owner->impl_->candidate_loop());
		owner->impl_->settled = true;
		owner->impl_->diagnostic.phase =
			owner->impl_->terminal_fenced ? CoordinatorPhase::Fenced : CoordinatorPhase::Closed;
	} catch (...) {
		// Group admission can fail after starting its first child. Never let
		// scheduler failure abandon a sibling's accepted original or worker.
		std::terminate();
	}
}
void Coordinator::cancel_update() noexcept {
	++impl_->cancellation;
	impl_->network_cancel.emit(asio::cancellation_type::terminal);
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
