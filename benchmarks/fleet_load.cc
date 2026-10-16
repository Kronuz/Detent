#include "fleet_load.hh"
#include "../src/detent_delivery/client_probe_internal.hh"
#include "detent_delivery/reconnect_backoff.hh"
#include <asio.hpp>
#include <asio/experimental/channel.hpp>
#include <atomic>
#include <bit>
#include <cstdio>
#include <fcntl.h>
#include <mutex>
#include <rapidjson/document.h>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>

namespace detent::fleet {
namespace {
namespace t = delivery;
namespace d = distribution;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
struct Run;
struct Owner;
struct Slot {
	Owner *owner = nullptr;
	std::size_t index = 0;
	asio::cancellation_signal cancellation;
	asio::steady_timer *bootstrap_wait = nullptr;
	t::SessionHandle session;
	std::optional<t::PublicationFeedback> feedback;
	std::optional<t::detail::ProbeOffer> last;
	std::uint64_t offers = 0, measured_offers = 0;
	std::int64_t last_measure_ms = 0, maximum_silence_ms = 0;
	bool opened = false, ready = false, checked = false, checkpoint_started = false;
};
struct Counters {
	std::atomic<std::uint64_t> accepted{0}, completed{0}, opened{0}, ready{0}, checked{0};
	std::atomic<std::uint64_t> offers{0}, idle{0}, retries{0}, logical_bytes{0}, wire_bytes{0};
	std::atomic<std::uint64_t> exchanges{0}, peak_exchanges{0}, closes{0}, skipped_closes{0};
	std::atomic<std::uint64_t> parked{0}, peak_parked{0}, premature_ready_polls{0};
	std::array<std::atomic<std::uint64_t>, 64> latency{};
};
struct Owner {
	using Channel = asio::experimental::channel<void(asio::error_code, unsigned)>;
	Run &run;
	std::size_t index;
	asio::strand<asio::io_context::executor_type> executor;
	Channel permits, closes;
	Owner(Run &, std::size_t, std::size_t);
};
struct Permit {
	Owner *owner = nullptr;
	Owner::Channel *channel = nullptr;
	explicit Permit(Owner &value, Owner::Channel &pool);
	Permit(Permit &&other) noexcept : owner(std::exchange(other.owner, nullptr)), channel(other.channel) {}
	Permit(const Permit &) = delete;
	~Permit();
};
struct Run {
	Options options;
	asio::io_context io;
	asio::strand<asio::io_context::executor_type> control{asio::make_strand(io)};
	asio::steady_timer wake{control};
	asio::signal_set signals{io, SIGINT, SIGTERM};
	std::shared_ptr<d::StorageLedger> account;
	d::StorageLedger::Ticket slots_ticket;
	std::unique_ptr<Slot[]> slots;
	std::vector<std::unique_ptr<Owner>> owners;
	Counters counts;
	std::atomic<bool> stopping{false}, bootstrap_complete{false};
	std::atomic<std::uint64_t> goal{0};
	std::atomic<std::int64_t> measurement_begin_ms{0}, measurement_end_ms{0};
	std::atomic<std::int64_t> cleanup_until_ms{INT64_MAX};
	std::array<unsigned char, 32> expected{};
	std::mutex failure_mutex;
	std::exception_ptr failure;
	bool protocol_complete = false, ready_reported = false;
	double measurement_cpu_begin = 0, measurement_cpu_end = 0;
	Clock::time_point started = Clock::now();
	std::unique_ptr<FILE, decltype(&std::fclose)> result{nullptr, &std::fclose};
	asio::posix::stream_descriptor input{control};
	explicit Run(Options value)
		: options(std::move(value)), account(std::make_shared<d::StorageLedger>(options.heap_bytes)) {
		std::size_t maximum_names = 0;
		for (const auto &source : options.sources) {
			maximum_names = std::max(maximum_names, source.namespaces.size());
			if (source.configured != options.sources.front().configured)
				throw std::invalid_argument("fleet source identities differ");
		}
		const auto required_heap = options.subscribers * (16384ull + maximum_names * 1024) +
								   256ull * 1024 * 1024 +
								   (options.active + 64) * (512ull * 1024 + maximum_names * 512);
		if (required_heap > options.heap_bytes)
			throw std::invalid_argument("fleet slot, exchange and reference admission exceeds heap budget");
		rlimit descriptors{};
		if (::getrlimit(RLIMIT_NOFILE, &descriptors) != 0)
			throw std::system_error(errno, std::generic_category(), "fleet descriptor preflight");
		if (descriptors.rlim_cur != RLIM_INFINITY && descriptors.rlim_cur < options.active + 256)
			throw std::invalid_argument(
				"fleet descriptor limit is below active exchanges plus control headroom");

		slots_ticket = account->admit(sizeof(Slot) * options.subscribers);
		slots = std::make_unique<Slot[]>(options.subscribers);
		owners.reserve(options.workers);
		for (std::size_t index = 0; index != options.workers; ++index) {
			const auto capacity =
				options.active / options.workers + (index < options.active % options.workers);
			owners.push_back(std::make_unique<Owner>(*this, index, capacity));
		}
		for (std::size_t index = 0; index != options.subscribers; ++index) {
			slots[index].index = index;
			slots[index].owner = owners[index % owners.size()].get();
		}
		for (auto &source : options.sources)
			source.limits.storage_account = account;
		const auto descriptor =
			::open(options.result.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
		if (descriptor < 0)
			throw std::system_error(errno, std::generic_category(), "fleet result open");
		result.reset(::fdopen(descriptor, "w"));
		if (!result) {
			::close(descriptor);
			throw std::system_error(errno, std::generic_category(), "fleet result stream");
		}
		const auto descriptor_input = ::fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
		if (descriptor_input < 0)
			throw std::system_error(errno, std::generic_category(), "fleet control input");
		asio::error_code input_error;
		input.assign(descriptor_input, input_error);
		if (input_error) {
			::close(descriptor_input);
			throw asio::system_error(input_error);
		}
	}
	void fail(std::exception_ptr value) {
		{
			std::lock_guard lock(failure_mutex);
			if (!failure)
				failure = value;
		}
		stop();
	}
	void notify() {
		asio::post(control, [this] { wake.cancel(); });
	}
	void stop() {
		auto unset = INT64_MAX;
		cleanup_until_ms.compare_exchange_strong(unset, std::chrono::duration_cast<std::chrono::milliseconds>(
															(Clock::now() + 20s).time_since_epoch())
															.count());
		if (!stopping.exchange(true, std::memory_order_acq_rel))
			for (auto &owner : owners)
				asio::co_spawn(owner->executor, cancel(*owner), [this](std::exception_ptr error) {
					if (error) {
						std::lock_guard lock(failure_mutex);
						if (!failure)
							failure = error;
					}
				});
		asio::post(control, [this] {
			protocol_cancellation.emit(asio::cancellation_type::all);
			asio::error_code ignored;
			input.cancel(ignored);
			signals.cancel(ignored);
			wake.cancel();
		});
	}
	asio::awaitable<void> wake_bootstrap(Owner &owner) {
		for (std::size_t index = owner.index; index < options.subscribers; index += owners.size()) {
			if (auto *wait = slots[index].bootstrap_wait)
				wait->cancel();
			if ((index / owners.size()) % 64 == 63)
				co_await asio::post(owner.executor, asio::use_awaitable);
		}
	}
	void became_ready(Slot &slot) {
		slot.ready = true;
		if (++counts.ready == options.subscribers) {
			bootstrap_complete.store(true, std::memory_order_release);
			for (auto &owner : owners)
				asio::co_spawn(owner->executor, wake_bootstrap(*owner), [this](std::exception_ptr error) {
					if (error)
						fail(error);
				});
		}
		notify();
	}
	asio::awaitable<void> cancel(Owner &owner) {
		for (std::size_t index = owner.index; index < options.subscribers; index += owners.size()) {
			slots[index].cancellation.emit(asio::cancellation_type::all);
			if ((index / owners.size()) % 64 == 63)
				co_await asio::post(owner.executor, asio::use_awaitable);
		}
	}
	asio::awaitable<Permit> acquire(Owner &owner) {
		co_await owner.permits.async_receive(asio::use_awaitable);
		co_return Permit(owner, owner.permits);
	}
	asio::awaitable<Permit> acquire_close(Owner &owner) {
		co_await owner.closes.async_receive(asio::use_awaitable);
		co_return Permit(owner, owner.closes);
	}
	asio::awaitable<void> release(t::ClientOptions source, t::SessionHandle &session) {
		co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
		source.timeout = std::min(source.timeout, 1000ms);
		std::exception_ptr error;
		try {
			co_await t::close_session(std::move(source), session);
		} catch (...) {
			error = std::current_exception();
		}
		session = {};
		if (error) {
			try {
				std::rethrow_exception(error);
			} catch (const t::SourceUnavailableError &) {
			} catch (const asio::system_error &) {
			} catch (const t::SessionResetError &reset) {
				if (!retryable(reset.reason()))
					throw;
			}
		}
	}
	asio::awaitable<void> close(Slot &slot, t::ClientOptions source) {
		co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
		if (slot.session.identity() != d::Identity{}) {
			auto permit = co_await acquire_close(*slot.owner);
			if (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
					.count() < cleanup_until_ms.load()) {
				++counts.closes;
				co_await release(std::move(source), slot.session);
			} else {
				++counts.skipped_closes;
				slot.session = {};
			}
		}
		slot.feedback.reset();
	}
	static bool cancelled(std::exception_ptr error) {
		try {
			std::rethrow_exception(error);
		} catch (const asio::system_error &network) {
			return network.code() == asio::error::operation_aborted;
		} catch (...) {
			return false;
		}
	}
	static bool retryable(t::SessionResetReason reason) {
		return reason != t::SessionResetReason::Forbidden && reason != t::SessionResetReason::Unauthorized &&
			   reason != t::SessionResetReason::IdentityChanged &&
			   reason != t::SessionResetReason::InvalidRequest;
	}
	asio::awaitable<void> subscriber(Slot &slot) {
		const auto &configured = options.sources[slot.index % options.sources.size()];
		t::ClientOptions source = configured;
		if (options.warm_revision) {
			source.base = source.configured;
			source.base->revision = *options.warm_revision;
		}
		t::ReconnectBackoff backoff(20ms, 1000ms, options.seed ^ slot.index);
		asio::steady_timer pause(slot.owner->executor);
		std::exception_ptr fatal;
		try {
			if (options.delay_bootstrap_slot && slot.index == *options.delay_bootstrap_slot) {
				pause.expires_after(options.bootstrap_delay);
				co_await pause.async_wait(asio::use_awaitable);
			}
			while (!stopping.load(std::memory_order_acquire)) {
				if (slot.ready && !bootstrap_complete.load(std::memory_order_acquire)) {
					pause.expires_at(Clock::time_point::max());
					slot.bootstrap_wait = &pause;
					const auto parked = ++counts.parked;
					auto peak = counts.peak_parked.load();
					while (parked > peak && !counts.peak_parked.compare_exchange_weak(peak, parked)) {
					}
					asio::error_code error;
					co_await pause.async_wait(asio::redirect_error(asio::use_awaitable, error));
					slot.bootstrap_wait = nullptr;
					--counts.parked;
					if (error && error != asio::error::operation_aborted)
						throw asio::system_error(error);
					if (!bootstrap_complete.load(std::memory_order_acquire) &&
						!stopping.load(std::memory_order_acquire))
						throw std::runtime_error("fleet bootstrap timer released before completion");
					continue;
				}
				const auto checkpoint = goal.load(std::memory_order_acquire);
				if (checkpoint && !slot.checked && slot.last && slot.last->target.revision == checkpoint &&
					slot.last->kind == d::UpdateKind::Snapshot) {
					if (slot.last->digest != expected)
						throw std::runtime_error("fleet checkpoint digest mismatch");
					slot.checked = true;
					++counts.checked;
					notify();
					break;
				}
				if (checkpoint && !slot.checked && !slot.checkpoint_started) {
					slot.checkpoint_started = true;
					co_await close(slot, source);
					source.base = source.configured;
					source.base->revision = checkpoint;
					co_await asio::this_coro::reset_cancellation_state(asio::enable_terminal_cancellation());
				}
				std::exception_ptr attempt_error;
				const auto began = Clock::now();
				try {
					auto permit = co_await acquire(*slot.owner);
					if (stopping.load(std::memory_order_acquire))
						break;
					if (goal.load(std::memory_order_acquire) != checkpoint)
						continue;
					if (!slot.session.valid()) {
						slot.session =
							co_await t::open_session(source, !options.warm_revision || checkpoint != 0);
						if (!slot.opened) {
							slot.opened = true;
							++counts.opened;
						}
					} else {
						if (slot.ready && !bootstrap_complete.load(std::memory_order_acquire))
							++counts.premature_ready_polls;
						auto event = co_await t::detail::ProbeAccess::next(
							source, slot.session, slot.feedback,
							options.warm_revision && !slot.ready
								? 0ms
								: std::min({slot.session.maximum_wait(), 30000ms, source.timeout / 2}));
						if (const auto *offer = std::get_if<t::detail::ProbeOffer>(&event)) {
							if (source.base && offer->target.revision < source.base->revision)
								throw std::runtime_error("fleet cursor regressed");
							slot.last = *offer;
							source.base = offer->target;
							slot.feedback = t::PublicationFeedback{offer->window, offer->target};
							const auto delivered_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
														  Clock::now().time_since_epoch())
														  .count();
							const auto begin = measurement_begin_ms.load(std::memory_order_acquire);
							const auto end = measurement_end_ms.load(std::memory_order_acquire);
							if (begin && delivered_ms >= begin && (!end || delivered_ms <= end)) {
								++slot.measured_offers;
								slot.maximum_silence_ms =
									std::max(slot.maximum_silence_ms,
											 delivered_ms - std::max(begin, slot.last_measure_ms));
								slot.last_measure_ms = delivered_ms;
							}
							++slot.offers;
							++counts.offers;
							counts.logical_bytes += offer->logical_bytes;
							counts.wire_bytes += offer->wire_bytes;
							if (!slot.ready)
								became_ready(slot);
							backoff.reset();
						} else if (std::holds_alternative<t::SessionIdle>(event)) {
							++counts.idle;
							if (options.warm_revision && !slot.ready)
								became_ready(slot);
							backoff.reset();
						} else
							throw t::SessionResetError(std::get<t::SessionReset>(event).reason);
					}
				} catch (...) {
					attempt_error = std::current_exception();
				}
				const auto elapsed =
					std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - began).count();
				++counts.latency[std::min<std::size_t>(
					63, std::bit_width(static_cast<std::uint64_t>(std::max<std::int64_t>(1, elapsed))))];
				if (attempt_error) {
					bool retry = false;
					try {
						std::rethrow_exception(attempt_error);
					} catch (const t::SourceUnavailableError &) {
						retry = true;
					} catch (const asio::system_error &) {
						retry = !stopping.load(std::memory_order_acquire);
					} catch (const t::SessionResetError &error) {
						retry = retryable(error.reason());
					} catch (...) {
						throw;
					}
					if (stopping.load(std::memory_order_acquire))
						break;
					if (!retry)
						std::rethrow_exception(attempt_error);
					++counts.retries;
					co_await close(slot, source);
					co_await asio::this_coro::reset_cancellation_state(asio::enable_terminal_cancellation());
					pause.expires_after(backoff.next());
					co_await pause.async_wait(asio::use_awaitable);
				}
			}
		} catch (...) {
			fatal = std::current_exception();
		}
		if (slot.bootstrap_wait) {
			slot.bootstrap_wait = nullptr;
			--counts.parked;
		}
		co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
		try {
			co_await close(slot, std::move(source));
		} catch (...) {
			if (!fatal)
				fatal = std::current_exception();
		}
		if (fatal) {
			try {
				std::rethrow_exception(fatal);
			} catch (const asio::system_error &error) {
				if (!stopping.load(std::memory_order_acquire) ||
					error.code() != asio::error::operation_aborted)
					throw;
			}
		}
	}
	asio::awaitable<void> protocol() {
		std::string line;
		std::uint64_t revision = 0;
		for (;;) {
			co_await asio::async_read_until(input, asio::dynamic_buffer(line, 256), '\n',
											asio::use_awaitable);
			const auto length = line.find('\n');
			rapidjson::Document command;
			command.Parse(line.data(), length);
			line.erase(0, length + 1);
			if (command.HasParseError() || !command.IsObject() || command.MemberCount() != 1)
				throw std::invalid_argument("invalid fleet control command");
			if (command.HasMember("measurement") && command["measurement"].IsBool()) {
				const bool begin = command["measurement"].GetBool();
				if ((begin && measurement_begin_ms.load()) ||
					(!begin && (!measurement_begin_ms.load() || measurement_end_ms.load())))
					throw std::invalid_argument("invalid fleet measurement transition");
				const auto now =
					std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
						.count();
				(begin ? measurement_begin_ms : measurement_end_ms).store(now, std::memory_order_release);
				rusage usage{};
				if (::getrusage(RUSAGE_SELF, &usage) != 0)
					throw std::system_error(errno, std::generic_category(), "fleet measurement CPU sample");
				(begin ? measurement_cpu_begin : measurement_cpu_end) =
					usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0 + usage.ru_stime.tv_sec +
					usage.ru_stime.tv_usec / 1000000.0;
				std::printf("{\"event\":\"measurement\",\"running\":%u}\n", begin ? 1u : 0u);
				std::fflush(stdout);
				continue;
			}
			if (!command.HasMember("checkpoint") || !command["checkpoint"].IsUint64() ||
				!command["checkpoint"].GetUint64() ||
				(measurement_begin_ms.load() && !measurement_end_ms.load()))
				throw std::invalid_argument("invalid fleet checkpoint command");
			revision = command["checkpoint"].GetUint64();
			break;
		}
		auto source = options.sources.front();
		source.base = source.configured;
		source.base->revision = revision;
		t::SessionHandle session;
		std::exception_ptr fatal;
		bool validated = false;
		try {
			asio::steady_timer retry(control);
			while (!stopping.load(std::memory_order_acquire)) {
				std::exception_ptr error;
				try {
					if (!session.valid())
						session = co_await t::open_session(source, true);
					auto event = co_await t::detail::ProbeAccess::reference(source, session, {}, 0ms);
					if (const auto *offer = std::get_if<t::detail::ProbeOffer>(&event)) {
						if (offer->kind != d::UpdateKind::Snapshot || offer->target.revision != revision)
							throw std::runtime_error(
								"fleet reference is not the requested checkpoint snapshot");
						expected = offer->digest;
						validated = true;
						break;
					}
					if (const auto *reset = std::get_if<t::SessionReset>(&event))
						throw t::SessionResetError(reset->reason);
					throw std::runtime_error("fleet forced reference snapshot returned Idle");
				} catch (...) {
					error = std::current_exception();
				}
				if (error) {
					try {
						std::rethrow_exception(error);
					} catch (const t::SourceUnavailableError &) {
					} catch (const t::SessionResetError &reset) {
						if (!retryable(reset.reason()))
							throw;
					} catch (const asio::system_error &network) {
						if (network.code() == asio::error::operation_aborted)
							throw;
					}
					co_await release(source, session);
					co_await asio::this_coro::reset_cancellation_state(asio::enable_terminal_cancellation());
					retry.expires_after(100ms);
					co_await retry.async_wait(asio::use_awaitable);
				}
			}
		} catch (...) {
			fatal = std::current_exception();
		}
		co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
		try {
			co_await release(std::move(source), session);
		} catch (...) {
			if (!fatal)
				fatal = std::current_exception();
		}

		if (fatal)
			std::rethrow_exception(fatal);
		if (validated) {
			goal.store(revision, std::memory_order_release);
			std::printf("{\"event\":\"checkpoint\",\"revision\":%llu}\n",
						static_cast<unsigned long long>(revision));
			std::fflush(stdout);
			notify();
		}
	}
	void sample() {
		std::printf("{\"event\":\"sample\",\"opened\":%llu,\"ready\":%llu,\"checked\":%llu,\"offers\":%llu,"
					"\"idle\":%llu,\"retries\":%llu,\"logical_bytes\":%llu,\"wire_bytes\":%llu,\"admitted_"
					"heap\":%zu,\"active_exchanges\":%llu,\"bootstrap_parked\":%llu}\n",
					static_cast<unsigned long long>(counts.opened.load()),
					static_cast<unsigned long long>(counts.ready.load()),
					static_cast<unsigned long long>(counts.checked.load()),
					static_cast<unsigned long long>(counts.offers.load()),
					static_cast<unsigned long long>(counts.idle.load()),
					static_cast<unsigned long long>(counts.retries.load()),
					static_cast<unsigned long long>(counts.logical_bytes.load()),
					static_cast<unsigned long long>(counts.wire_bytes.load()), account->used(),
					static_cast<unsigned long long>(counts.exchanges.load()),
					static_cast<unsigned long long>(counts.parked.load()));
		std::fflush(stdout);
	}
	asio::cancellation_signal protocol_cancellation;
	asio::awaitable<void> supervise() {
		asio::co_spawn(
			control, protocol(),
			asio::bind_cancellation_slot(protocol_cancellation.slot(), [this](std::exception_ptr error) {
				protocol_complete = true;
				if (error && !(stopping.load(std::memory_order_acquire) && cancelled(error)))
					fail(error);
				notify();
			}));
		try {
			for (std::size_t index = 0; index != options.subscribers; ++index) {
				if (options.fail_launch_after && index == *options.fail_launch_after)
					throw std::runtime_error("injected fleet partial launch failure");
				if (stopping.load(std::memory_order_acquire))
					break;
				auto &slot = slots[index];
				++counts.accepted;
				try {
					asio::co_spawn(slot.owner->executor, subscriber(slot),
								   asio::bind_cancellation_slot(slot.cancellation.slot(),
																[this](std::exception_ptr error) {
																	++counts.completed;
																	if (error)
																		fail(error);
																	notify();
																}));
				} catch (...) {
					--counts.accepted;
					throw;
				}
				if (index % 64 == 63)
					co_await asio::post(control, asio::use_awaitable);
			}
		} catch (...) {
			fail(std::current_exception());
		}
		auto sample_at = Clock::now();
		while (!stopping.load(std::memory_order_acquire)) {
			if (!ready_reported && bootstrap_complete.load(std::memory_order_acquire)) {
				ready_reported = true;
				std::printf("{\"event\":\"ready\",\"sessions\":%zu,\"active_limit\":%zu,\"warm\":%s}\n",
							options.subscribers, options.active, options.warm_revision ? "true" : "false");
				std::fflush(stdout);
			}
			if (counts.checked.load() == options.subscribers) {
				stop();
				break;
			}
			if (Clock::now() >= started + options.lifetime) {
				fail(std::make_exception_ptr(std::runtime_error("fleet checkpoint lifetime exceeded")));
				break;
			}
			if (Clock::now() >= sample_at) {
				sample();
				sample_at = Clock::now() + 1s;
			}
			wake.expires_at(std::min(sample_at, started + options.lifetime));
			asio::error_code error;
			co_await wake.async_wait(asio::redirect_error(asio::use_awaitable, error));
		}
		protocol_cancellation.emit(asio::cancellation_type::all);
		input.cancel();
		while (counts.completed.load() != counts.accepted.load() || !protocol_complete) {
			wake.expires_after(1s);
			asio::error_code error;
			co_await wake.async_wait(asio::redirect_error(asio::use_awaitable, error));
		}
		signals.cancel();
		if (counts.checked.load() != options.subscribers) {
			std::lock_guard lock(failure_mutex);
			if (!failure)
				failure =
					std::make_exception_ptr(std::runtime_error("fleet stopped before complete checkpoint"));
		}
	}
	int finish() {
		std::uint64_t minimum = UINT64_MAX, maximum = 0;
		for (std::size_t index = 0; index != options.subscribers; ++index) {
			minimum = std::min(minimum, slots[index].offers);
			maximum = std::max(maximum, slots[index].offers);
		}
		if (account->used() != slots_ticket.bytes() || counts.exchanges.load() || counts.parked.load())
			throw std::runtime_error("fleet admitted resources did not drain to retained slots");
		rusage usage{};
		if (::getrusage(RUSAGE_SELF, &usage) != 0)
			throw std::system_error(errno, std::generic_category(), "fleet resource sample");
		const auto cpu = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0 + usage.ru_stime.tv_sec +
						 usage.ru_stime.tv_usec / 1000000.0;
#ifdef __APPLE__
		const auto rss = static_cast<std::uint64_t>(usage.ru_maxrss);
#else
		const auto rss = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
		const auto measurement_begin = measurement_begin_ms.load();
		const auto measurement_end = measurement_end_ms.load();
		std::uint64_t measured_min = UINT64_MAX, measured_max = 0, silent = 0;
		std::int64_t maximum_silence = 0;
		for (std::size_t index = 0; index != options.subscribers; ++index) {
			const auto &slot = slots[index];
			measured_min = std::min(measured_min, slot.measured_offers);
			measured_max = std::max(measured_max, slot.measured_offers);
			silent += slot.measured_offers == 0;
			if (measurement_begin && measurement_end)
				maximum_silence =
					std::max({maximum_silence, slot.maximum_silence_ms,
							  measurement_end - std::max(measurement_begin, slot.last_measure_ms)});
		}
		std::fprintf(
			result.get(),
			"{\"measured_ms\":%lld,\"measured_minimum_offers\":%llu,\"measured_maximum_offers\":%llu,"
			"\"measured_silent_probes\":%llu,\"measured_maximum_silence_ms\":%lld,"
			"\"measured_cpu_seconds\":%.6f,\"peak_bootstrap_parked\":%llu,"
			"\"premature_ready_polls\":%llu,",
			static_cast<long long>(measurement_end ? measurement_end - measurement_begin : 0),
			static_cast<unsigned long long>(measured_min), static_cast<unsigned long long>(measured_max),
			static_cast<unsigned long long>(silent), static_cast<long long>(maximum_silence),
			measurement_end ? measurement_cpu_end - measurement_cpu_begin : 0,
			static_cast<unsigned long long>(counts.peak_parked.load()),
			static_cast<unsigned long long>(counts.premature_ready_polls.load()));
		std::fprintf(result.get(),
					 "\"success\":%s,\"transport_probes\":%zu,\"durable_stores\":0,\"active_limit\":%zu,"
					 "\"workers\":%zu,\"accepted\":%llu,\"completed\":%llu,\"opened\":%llu,\"ready\":%llu,"
					 "\"checked\":%llu,\"checkpoint_revision\":%llu,\"offers\":%llu,\"minimum_offers\":%llu,"
					 "\"maximum_offers\":%llu,\"logical_bytes\":%llu,\"wire_bytes\":%llu,\"retries\":%llu,"
					 "\"admitted_peak_bytes\":%zu,\"retained_slot_bytes\":%zu,\"peak_exchanges\":%llu,\"cpu_"
					 "seconds\":%.6f,\"maximum_rss_bytes\":%llu,\"elapsed_seconds\":%.6f,"
					 "\"warm_start\":%s,\"close_limit\":64,\"reference_limit\":1,"
					 "\"exchange_latency_us_log2\":[",
					 failure ? "false" : "true", options.subscribers, options.active, options.workers,
					 static_cast<unsigned long long>(counts.accepted.load()),
					 static_cast<unsigned long long>(counts.completed.load()),
					 static_cast<unsigned long long>(counts.opened.load()),
					 static_cast<unsigned long long>(counts.ready.load()),
					 static_cast<unsigned long long>(counts.checked.load()),
					 static_cast<unsigned long long>(goal.load()),
					 static_cast<unsigned long long>(counts.offers.load()),
					 static_cast<unsigned long long>(minimum), static_cast<unsigned long long>(maximum),
					 static_cast<unsigned long long>(counts.logical_bytes.load()),
					 static_cast<unsigned long long>(counts.wire_bytes.load()),
					 static_cast<unsigned long long>(counts.retries.load()), account->peak(),
					 slots_ticket.bytes(), static_cast<unsigned long long>(counts.peak_exchanges.load()), cpu,
					 static_cast<unsigned long long>(rss),
					 std::chrono::duration<double>(Clock::now() - started).count(),
					 options.warm_revision ? "true" : "false");
		for (std::size_t index = 0; index != counts.latency.size(); ++index)
			std::fprintf(result.get(), "%s%llu", index ? "," : "",
						 static_cast<unsigned long long>(counts.latency[index].load()));
		std::fprintf(result.get(),
					 "],\"close_attempts\":%llu,\"skipped_closes\":%llu,\"checkpoint_sha256\":\"",
					 static_cast<unsigned long long>(counts.closes.load()),
					 static_cast<unsigned long long>(counts.skipped_closes.load()));
		for (const auto byte : expected)
			std::fprintf(result.get(), "%02x", static_cast<unsigned>(byte));
		std::fputs("\"}\n", result.get());
		if (std::fflush(result.get()) != 0 || std::ferror(result.get()))
			throw std::runtime_error("fleet result write failed");
		if (failure)
			std::rethrow_exception(failure);
		return 0;
	}
};
Owner::Owner(Run &value, std::size_t ordinal, std::size_t capacity)
	: run(value), index(ordinal), executor(asio::make_strand(value.io)), permits(executor, capacity),
	  closes(executor, 64 / value.options.workers + (ordinal < 64 % value.options.workers)) {
	for (std::size_t token = 0; token != capacity; ++token)
		if (!permits.try_send(asio::error_code{}, 0u))
			throw std::logic_error("fleet permit initialization");
	const auto close_capacity = 64 / value.options.workers + (ordinal < 64 % value.options.workers);
	for (std::size_t token = 0; token != close_capacity; ++token)
		if (!closes.try_send(asio::error_code{}, 0u))
			throw std::logic_error("fleet Close permit initialization");
}
Permit::Permit(Owner &value, Owner::Channel &pool) : owner(&value), channel(&pool) {
	const auto active = ++owner->run.counts.exchanges;
	auto peak = owner->run.counts.peak_exchanges.load();
	while (active > peak && !owner->run.counts.peak_exchanges.compare_exchange_weak(peak, active)) {
	}
}
Permit::~Permit() {
	if (owner) {
		--owner->run.counts.exchanges;
		if (!channel->try_send(asio::error_code{}, 0u))
			std::terminate();
	}
}
} // namespace
int run(Options options) {
	Run state(std::move(options));
	state.signals.async_wait(asio::bind_executor(state.control, [&state](const asio::error_code &error, int) {
		if (!error)
			state.stop();
	}));
	asio::co_spawn(state.control, state.supervise(), [&state](std::exception_ptr error) {
		if (error)
			state.fail(error);
	});
	std::vector<std::jthread> workers;
	workers.reserve(state.options.workers - 1);
	try {
		for (std::size_t index = 1; index < state.options.workers; ++index)
			workers.emplace_back([&state] { state.io.run(); });
	} catch (...) {
		state.fail(std::current_exception());
	}
	state.io.run();
	for (auto &worker : workers)
		worker.join();
	return state.finish();
}
} // namespace detent::fleet
