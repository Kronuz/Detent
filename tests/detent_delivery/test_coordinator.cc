#include "../../src/detent_delivery/coordinator_internal.hh"
#include "detent/hashing.hh"
#include "detent_delivery/coordinator.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_local/paged_layout.hh"
#include "detent_relay/bundle.hh"
#include "detent_relay/sessions.hh"
#include <asio/experimental/awaitable_operators.hpp>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <source_location>
#include <thread>
#include <unistd.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace t = detent::delivery;
namespace m = d::messagepack;
using namespace std::chrono_literals;
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("coordinator invariant at line " + std::to_string(location.line()));
}
#ifdef DETENT_TEST_RELAY_HTTP
#include "relay_network_fixture.hh"
#include "relay_service_fixture.hh"
#endif
class RecordingSink final : public t::PublicationSink {
  public:
	void authenticated() noexcept override { ++authentications; }
	void begin_stage(const t::ReceivedUpdate &update) override {
		check(!borrowed_snapshot && !borrowed_delta);
		borrowed_snapshot = update.snapshot;
		borrowed_delta = update.delta;
		snapshot_weak = update.snapshot;
		delta_weak = update.delta;
		steps = 4;
		++stages;
	}
	bool stage_step(d::WorkBudget &budget) override {
		check(borrowed_snapshot || borrowed_delta);
		if (!budget.bytes || !budget.tokens)
			return false;
		--budget.bytes;
		--budget.tokens;
		if (--steps)
			return false;
		borrowed_snapshot.reset();
		borrowed_delta.reset();
		return true;
	}
	void bind_candidate(const l::Descriptor &descriptor, l::DirectoryIdentity directory) noexcept override {
		candidate = descriptor;
		identity = directory;
	}
	bool release_stage(d::WorkBudget &) noexcept override {
		borrowed_snapshot.reset();
		borrowed_delta.reset();
		return true;
	}
	void selected(const l::RecoveredGeneration &generation) override {
		check(snapshot_weak.expired() && delta_weak.expired());
		if (candidate && *candidate == generation.descriptor()) {
			check(identity == generation.directory_identity());
			++commits;
		}
		snapshot_weak.reset();
		delta_weak.reset();
		candidate.reset();
		last_selected = generation.descriptor();
	}
	void fence() noexcept override {
		fenced = true;
		borrowed_snapshot.reset();
		borrowed_delta.reset();
		snapshot_weak.reset();
		delta_weak.reset();
		candidate.reset();
	}
	unsigned authentications = 0, stages = 0, commits = 0;
	bool fenced = false;
	std::optional<l::Descriptor> last_selected;

  private:
	std::shared_ptr<const d::ValidatedSnapshot> borrowed_snapshot;
	std::shared_ptr<const d::ValidatedDelta> borrowed_delta;
	std::weak_ptr<const d::ValidatedSnapshot> snapshot_weak;
	std::weak_ptr<const d::ValidatedDelta> delta_weak;
	std::optional<l::Descriptor> candidate;
	l::DirectoryIdentity identity;
	unsigned steps = 0;
};

d::Cursor cursor() {
	d::Cursor value;
	value.cluster.fill('c');
	value.group.fill('g');
	value.restore_epoch.fill('e');
	value.scope.fill('s');
	value.revision = 1;
	return value;
}
std::string response(std::uint32_t count, std::uint64_t revision = 1) {
	std::string wire;
	auto a = [&](auto n) { m::array(wire, n); };
	auto u = [&](auto n) { m::unsigned_integer(wire, n); };
	a(7);
	u(1);
	u(1);
	a(2);
	u(1);
	u(1);
	a(5);
	auto identity = cursor();
	for (auto *part : {&identity.cluster, &identity.group, &identity.restore_epoch})
		m::string(wire, {part->data(), part->size()}, true);
	m::string(wire, {identity.scope.data(), identity.scope.size()}, true);
	u(revision);
	a(4);
	const auto overrides = count - (count + 3) / 4;
	u(count ? 1 : 0);
	u(count);
	u(overrides);
	u(count ? 3 + std::uint64_t(count) * 13 + std::uint64_t(overrides) * 4 : 0);
	a(count ? 1 : 0);
	if (count) {
		a(2);
		m::string(wire, "app");
		u(1);
	}
	a(count);
	for (std::uint32_t i = 0; i < count; ++i) {
		char digits[7];
		auto end = std::to_chars(digits, digits + 7, i).ptr;
		std::string key = "app:key";
		key.append(6 - (end - digits), '0');
		key.append(digits, end);
		a(4);
		u(0);
		m::string(wire, key);
		u(1);
		if (i % 4 == 0) {
			a(2);
			u(1);
			m::signed_integer(wire, -std::int64_t(i));
		} else {
			a(4);
			u(0);
			wire.push_back(i % 2 ? '\xc3' : '\xc2');
			u(25);
			a(1);
			a(3);
			m::string(wire, "beta");
			u(1);
			u(17);
		}
	}
	d::Identity update;
	update.fill('u');
	std::string frame;
	for (std::size_t offset = 0; offset < wire.size();) {
		const auto length = std::min<std::size_t>(65536, wire.size() - offset);
		frame += d::encode_fragment({d::UpdateKind::Snapshot, update, offset, wire.size(),
									 offset + length == wire.size(), wire.substr(offset, length)},
									false);
		offset += length;
	}
	return "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.detent.distribution\r\n"
		   "X-Detent-Profile: 1\r\nX-Detent-Revision: " +
		   std::to_string(revision) +
		   "\r\nX-Detent-Kind: 1\r\n"
		   "X-Detent-Coding: raw\r\nContent-Length: " +
		   std::to_string(frame.size()) + "\r\n\r\n" + frame;
}
std::string transition_response(bool progress) {
	std::string wire;
	auto a = [&](unsigned n) { m::array(wire, n); };
	auto u = [&](std::uint64_t n) { m::unsigned_integer(wire, n); };
	auto put_cursor = [&](unsigned revision) {
		const auto value = cursor();
		a(5);
		for (const auto *part : {&value.cluster, &value.group, &value.restore_epoch})
			m::string(wire, {part->data(), part->size()}, true);
		m::string(wire, {value.scope.data(), value.scope.size()}, true);
		u(revision);
	};
	a(progress ? 5 : 7);
	u(1);
	u(progress ? 3 : 2);
	a(2);
	u(1);
	u(1);
	put_cursor(progress ? 1 : 2);
	put_cursor(progress ? 2 : 3);
	if (!progress) {
		a(4);
		u(1);
		u(1);
		u(0);
		u(16);
		a(1);
		a(3);
		u(3);
		a(1);
		a(2);
		m::string(wire, "app");
		u(3);
		a(1);
		a(4);
		u(0);
		m::string(wire, "app:key000004");
		u(0);
		a(2);
		u(1);
		m::signed_integer(wire, -4);
	}
	d::Identity identity{};
	identity.fill(progress ? 'p' : 'd');
	const auto kind = progress ? d::UpdateKind::Progress : d::UpdateKind::Delta;
	auto frame = d::encode_fragment({kind, identity, 0, wire.size(), true, wire}, false);
	return "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.detent.distribution\r\nX-Detent-Profile: "
		   "1\r\nX-Detent-Revision: " +
		   std::to_string(progress ? 2 : 3) + "\r\nX-Detent-Kind: " + std::to_string(progress ? 3 : 2) +
		   "\r\nX-Detent-Coding: raw\r\nContent-Length: " + std::to_string(frame.size()) + "\r\n\r\n" + frame;
}

struct Timing {
	std::uint64_t turns = 0;
	std::int64_t maximum_lateness_us = 0;
};
asio::awaitable<void> observe(std::shared_ptr<t::Coordinator> coordinator, Timing &timing,
							  bool cancel_selected, const std::atomic<bool> *feedback_received = nullptr,
							  std::chrono::milliseconds timeout = 60s, unsigned final_revision = 1) {
	auto executor = co_await asio::this_coro::executor;
	asio::steady_timer timer(executor);
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	if (cancel_selected) {
		while (coordinator->status().selected.knowledge != l::SelectionKnowledge::Known &&
			   std::chrono::steady_clock::now() < deadline)
			co_await asio::post(executor, asio::use_awaitable);
		const auto selected = coordinator->status();
		if (selected.acknowledged || selected.selected.knowledge != l::SelectionKnowledge::Known) {
			coordinator->close();
			throw std::runtime_error("cancel test missed actual selection boundary");
		}
		coordinator->cancel_update();
		check(!coordinator->status().acknowledged);
	}

	while (!coordinator->status().acknowledged && std::chrono::steady_clock::now() < deadline) {
		const auto expected = std::chrono::steady_clock::now() + 1ms;
		timer.expires_at(expected);
		co_await timer.async_wait(asio::use_awaitable);
		++timing.turns;
		timing.maximum_lateness_us = std::max<std::int64_t>(
			timing.maximum_lateness_us,
			std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - expected)
				.count());
	}
	const bool published = coordinator->status().acknowledged == cursor();
	while (published && coordinator->status().recoverable != cursor() &&
		   std::chrono::steady_clock::now() < deadline) {
		const auto expected = std::chrono::steady_clock::now() + 1ms;
		timer.expires_at(expected);
		co_await timer.async_wait(asio::use_awaitable);
		++timing.turns;
		timing.maximum_lateness_us = std::max<std::int64_t>(
			timing.maximum_lateness_us,
			std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - expected)
				.count());
	}
	if (feedback_received) {
		while (!feedback_received->load() && !coordinator->drained() &&
			   std::chrono::steady_clock::now() < deadline) {
			timer.expires_after(1ms);
			co_await timer.async_wait(asio::use_awaitable);
		}
		{
			const auto status = coordinator->status();
			auto elapsed = [&](std::chrono::steady_clock::time_point at) -> std::int64_t {
				return at == std::chrono::steady_clock::time_point{}
						   ? -1
						   : std::chrono::duration_cast<std::chrono::milliseconds>(at - (deadline - timeout)).count();
			};
			std::cerr << "coordinator feedback=" << (feedback_received->load() ? "received" : "deadline")
					  << " phase=" << static_cast<unsigned>(status.phase)
					  << " initialized=" << status.initialized << " worker_pending=" << status.worker_pending
					  << " received=" << (status.received ? status.received->revision : 0)
					  << " validated=" << (status.validated ? status.validated->revision : 0)
					  << " recoverable=" << (status.recoverable ? status.recoverable->revision : 0)
					  << " acknowledged=" << (status.acknowledged ? status.acknowledged->revision : 0)
					  << " ack_ms=" << elapsed(status.publication_acknowledged_at)
					  << " discard_ms=" << elapsed(status.discard_started_at)
					  << " recovery_ms=" << elapsed(status.recovery_started_at)
					  << '\n';
			for (std::size_t index = 0; index < status.cleanup.size(); ++index) {
				const auto &slice = status.cleanup[index];
				std::cerr << "coordinator cleanup=" << index << " turns=" << slice.turns
						  << " charged_bytes=" << slice.charged_bytes << " max_gap_ns=" << slice.maximum_gap_ns
						  << " finished_ms=" << elapsed(slice.finished) << '\n';
			}
			if (!feedback_received->load()) {
				coordinator->close();
				if (status.error)
					std::rethrow_exception(status.error);
			}
		}
		check(feedback_received->load());
	}
	// Close wakes the parked candidate after the source observes durable feedback.
	timer.expires_after(5ms);
	co_await timer.async_wait(asio::use_awaitable);
	coordinator->close();
	auto final = cursor();
	final.revision = final_revision;
	check(published && coordinator->status().recoverable == final);
}
asio::awaitable<void> observe_poll_backoff(std::shared_ptr<t::Coordinator> coordinator,
										   const std::atomic<bool> &server_completed) {
	asio::steady_timer timer(co_await asio::this_coro::executor);
	const auto deadline = std::chrono::steady_clock::now() + 15s;
	std::exception_ptr failure;
	try {
		bool observed = false;
		while (std::chrono::steady_clock::now() < deadline) {
			const auto retry = t::detail::CoordinatorAccess::reconnect_observation(*coordinator);
			if (retry.count >= 4 && server_completed.load(std::memory_order_acquire)) {
				if (retry.count != 4 || retry.delay < 10ms || retry.delay > 20ms)
					throw std::runtime_error(
						"poll reconnect delay did not reset: count=" + std::to_string(retry.count) +
						" delay_ms=" + std::to_string(retry.delay.count()));
				const auto state = coordinator->status();
				check(state.failures == 4 && state.acknowledged == cursor() &&
					  state.recoverable == cursor() && state.selected.descriptor &&
					  state.selected.descriptor->cursor == cursor() && !state.worker_pending &&
					  state.source_rotations == 0);
				observed = true;
				break;
			}
			check(!coordinator->drained());
			timer.expires_after(1ms);
			co_await timer.async_wait(asio::use_awaitable);
		}
		check(observed);
	} catch (...) {
		failure = std::current_exception();
	}
	coordinator->close();
	if (failure)
		std::rethrow_exception(failure);
}
asio::awaitable<void> observe_pressure(std::shared_ptr<t::Coordinator> coordinator,
									   const t::CoordinatorOptions &options) {
	asio::steady_timer timer(co_await asio::this_coro::executor);
	const auto deadline = std::chrono::steady_clock::now() + 60s;
	auto wait_for = [&](auto predicate) -> asio::awaitable<void> {
		while (!predicate() && std::chrono::steady_clock::now() < deadline) {
			timer.expires_after(1ms);
			co_await timer.async_wait(asio::use_awaitable);
		}
		if (!predicate()) {
			coordinator->close();
			throw std::runtime_error("pinned reader pressure deadline");
		}
	};
	auto expected = cursor();
	co_await wait_for([&] { return coordinator->status().acknowledged == expected; });
	// The test deliberately performs its small fixture recovery synchronously;
	// production SDK recovery runs on its configuration worker.
	auto pinned = l::recover_generation(options.directory, options.view_limits).view();
	check(pinned.cursor() == expected);
	expected.revision = 2;
	co_await wait_for([&] { return coordinator->status().acknowledged == expected; });
	co_await wait_for([&] {
		return coordinator->status().phase == t::CoordinatorPhase::WaitingPublication &&
			   coordinator->status().received && coordinator->status().received->revision == 3;
	});
	timer.expires_after(30ms);
	co_await timer.async_wait(asio::use_awaitable);
	check(coordinator->status().acknowledged == expected &&
		  coordinator->status().selected.descriptor->cursor == expected);
	check(pinned.cursor().revision == 1);
	pinned = {};
	expected.revision = 3;
	co_await wait_for([&] {
		return coordinator->status().acknowledged == expected &&
			   coordinator->status().recoverable == expected;
	});
	coordinator->close();
}
asio::awaitable<void> exercise(std::shared_ptr<t::Coordinator> coordinator, Timing &timing,
							   bool cancel_selected, bool pressure, const t::CoordinatorOptions &options,
							   const std::atomic<bool> *feedback_received, bool cancel_open = false,
							   bool transitions = false, bool worker_cancel = false,
							   bool maintenance_failure = false, bool poll_backoff = false) {
	using namespace asio::experimental::awaitable_operators;
	if (poll_backoff) {
		check(feedback_received != nullptr);
		co_await (coordinator->run() && observe_poll_backoff(coordinator, *feedback_received));
		co_return;
	}
	if (worker_cancel) {
		auto cancel = [&]() -> asio::awaitable<void> {
			asio::steady_timer timer(co_await asio::this_coro::executor);
			const auto deadline = std::chrono::steady_clock::now() + options.source.timeout;
			while (!(coordinator->status().worker_pending && coordinator->status().received) &&
				   std::chrono::steady_clock::now() < deadline) {
				timer.expires_after(1ms);
				co_await timer.async_wait(asio::use_awaitable);
			}
			const bool accepted = coordinator->status().worker_pending && coordinator->status().received;
			coordinator->cancel_update();
			coordinator->close();
			check(accepted);
		};
		co_await (coordinator->run() && cancel());
		check(!coordinator->status().worker_pending && !coordinator->status().acknowledged);
		co_return;
	}
	if (cancel_open || maintenance_failure) {
		co_await coordinator->run();
		co_return;
	}
	if (pressure) {
		co_await (coordinator->run() && observe_pressure(coordinator, options));
		co_return;
	}
	co_await (coordinator->run() && observe(coordinator, timing, cancel_selected, feedback_received,
											options.source.timeout, transitions ? 3 : 1));
}
void qualify_relay_sessions(std::shared_ptr<detent::relay::Bundle> bundle,
							d::RetirementHandle<d::EncodedUpdate> expected,
							std::shared_ptr<d::StorageLedger> control_account,
							std::shared_ptr<d::StorageLedger> cache_account) {
	namespace r = detent::relay;
	r::SessionAuthorization authorization;
	authorization.principal.identity.fill('p');
	authorization.principal.policy_epoch = 1;
	authorization.grants.fill('g');
	authorization.bundles[0] = 1;
	const std::array bindings{r::SessionBinding{bundle, authorization.principal, cursor()}};
	r::Sessions sessions(control_account, bindings, {500, 500});
	auto now = r::Sessions::Clock::now();
	auto id = [](unsigned number) {
		d::Identity identity;
		identity.fill('w');
		identity[14] = static_cast<char>(number >> 8);
		identity[15] = static_cast<char>(number);
		return identity;
	};
	auto forbidden = authorization;
	forbidden.bundles[0] = 0;
	check(sessions.open(id(500), forbidden, 0, {}, false, false, now) == d::SubscriptionResult::Forbidden);
	auto ahead = cursor();
	++ahead.revision;
	check(sessions.open(id(500), authorization, 0, ahead, false, false, now) ==
		  d::SubscriptionResult::SourceBehind);
	const auto cache_before = cache_account->used();
	const auto staged_before = bundle->status().staged;
	std::vector<r::PreparedOffer, d::StorageAllocator<r::PreparedOffer>> held{
		d::StorageAllocator<r::PreparedOffer>(control_account)};
	held.reserve(500);
	for (unsigned i = 0; i < 500; ++i) {
		check(sessions.open(id(i), authorization, 0, {}, true, bool(i % 2), now) ==
			  d::SubscriptionResult::Accepted);
		check(sessions.metadata(id(i), now)->compression == bool(i % 2));
		auto request = sessions.next(id(i), authorization, 0, {}, now);
		check(request.token.has_value());
		auto prepared = sessions.prepare(*request.token, now);
		check(prepared.result == d::SubscriptionResult::SnapshotReady && prepared.token &&
			  prepared.update.get() == expected.get());
		held.push_back(std::move(prepared));
	}
	const auto plan_bytes = sizeof(r::BootstrapPlan) + d::StorageAllocator<r::BootstrapPlan>::allocation_overhead();
	check(sessions.size() == 500 && cache_account->used() <= cache_before + 500 * plan_bytes &&
		  bundle->status().staged == staged_before);
	check(sessions.open(id(500), authorization, 0, {}, true, false, now) == d::SubscriptionResult::Full);
	check(sessions.next(id(1), forbidden, 1, cursor(), now).result == d::SubscriptionResult::Forbidden);
	auto other = authorization;
	other.principal.identity.fill('x');
	check(sessions.next(id(1), other, 1, cursor(), now).result == d::SubscriptionResult::Forbidden);
	other = authorization;
	++other.principal.policy_epoch;
	check(sessions.next(id(1), other, 1, cursor(), now).result == d::SubscriptionResult::IdentityChanged);
	auto early = sessions.next(id(0), authorization, 1, cursor(), now);
	check(early.token.has_value());
	check(sessions.prepare(*early.token, now).result == d::SubscriptionResult::AwaitSettlement);
	auto original_frame = expected->frame(0, false);
	bundle->fence();
	check(sessions.prepare(*early.token, now).result == d::SubscriptionResult::Forbidden);
	check(sessions.next(id(1), authorization, 1, cursor(), now).result == d::SubscriptionResult::Forbidden);
	check(!original_frame->empty() && held.back().update->frame(0, false).get() == original_frame.get());
	sessions.release(*early.token);
	check(sessions.close(id(1), authorization, now) == d::SubscriptionResult::Accepted);
	sessions.close_all();
	d::WorkBudget budget{8192, 500};
	sessions.collect(budget, now);
	check(sessions.size() == 500);
	for (auto &offered : held)
		sessions.settle(*offered.token, true);
	budget = {8192, 500};
	sessions.collect(budget, now);
	check(sessions.drained());
	std::cout << "500 relay offers share one encoded object; fencing retains offered frames\n";
}
int main(int argc, char **argv) {
	try {
		check(argc == 2 || argc == 3);
		const bool retained_idle = argc == 2;
		const bool paged = argc == 3 && std::string_view(argv[2]).find("paged") != std::string_view::npos;
		const bool transitions =
			paged && std::string_view(argv[2]).find("transitions") != std::string_view::npos;
		const bool compaction_pressure =
			paged && std::string_view(argv[2]).find("compaction-pressure") != std::string_view::npos;
		const bool worker_cancel =
			paged && std::string_view(argv[2]).find("worker-cancel") != std::string_view::npos;
		const bool migration = paged && std::string_view(argv[2]).find("migrate") != std::string_view::npos;
		const bool maintenance_failure =
			migration && std::string_view(argv[2]).find("recovery-failure") != std::string_view::npos;
		const auto original_maintenance_failure =
			std::make_exception_ptr(std::runtime_error("original maintenance recovery failure"));
		const bool relay = argc == 3 && std::string_view(argv[2]).find("relay") != std::string_view::npos;
		const bool relay_sessions =
			relay && std::string_view(argv[2]).find("sessions") != std::string_view::npos;
#ifdef DETENT_TEST_RELAY_HTTP
		const bool relay_http = relay && std::string_view(argv[2]).find("http") != std::string_view::npos;
		const bool relay_network =
			relay && std::string_view(argv[2]).find("network") != std::string_view::npos;
		const bool network_failures =
			relay_network && std::string_view(argv[2]).find("failures") != std::string_view::npos;
#endif
		const bool relay_pressure = relay && std::string_view(argv[2]).ends_with("pressure");
		const bool sink = argc == 3 && std::string_view(argv[2]).find("sink") != std::string_view::npos;
		const bool cancel_selected = argc == 3 && (std::string_view(argv[2]) == "--cancel-selected" ||
												   std::string_view(argv[2]) == "--sink-cancel-selected");
		const bool pressure = argc == 3 && std::string_view(argv[2]) == "--reader-pressure";
		const bool cancel_open = argc == 3 && std::string_view(argv[2]) == "--subscription-cancel-open";
		const bool reset_retry = argc == 3 && std::string_view(argv[2]) == "--subscription-reset";
		const bool subscription = argc == 3 && std::string_view(argv[2]).starts_with("--subscription");
		const bool poll_backoff = argc == 3 && std::string_view(argv[2]) == "--pool-poll-backoff";
		const auto requested = poll_backoff	  ? 4u
							   : subscription ? (std::string_view(argv[2]).ends_with("-500000")	  ? 500000u
												 : std::string_view(argv[2]).ends_with("-100000") ? 100000u
																								  : 0u)
							   : argc == 3 && !cancel_selected && !pressure && !sink ? std::stoul(argv[2])
																					 : 0;
		std::atomic<bool> feedback_received{false};
		check(requested <= 500000);
		const auto count = static_cast<std::uint32_t>(requested);
		const auto started = std::chrono::steady_clock::now();
		auto checkpoint = [&](std::string_view phase) {
			std::cerr << "coordinator records=" << count << " phase=" << phase << " elapsed_ms="
				<< std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - started).count() << '\n';
		};
		checkpoint("setup");
		const std::filesystem::path certificates(argv[1]);
		auto directory = std::filesystem::current_path() / ".scratch" /
						 ("delivery-coordinator-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto root = std::make_shared<d::StorageLedger>(384 * 1024 * 1024);
		auto account = std::make_shared<d::StorageLedger>(root, 256 * 1024 * 1024);
		d::RetirementRuntime runtime(account, 3);
		auto domain = runtime.domain(account, paged ? 2048 : 64);
		auto mapping = std::make_shared<d::StorageLedger>(512ull * 1024 * 1024);
		l::DiskStoreOptions disk_options;
		disk_options.mode = l::StoreOpenMode::FreshEmpty;
		if (paged) {
			disk_options.recovery_limits.mapping_account = mapping;
			disk_options.recovery_limits.retirement_domain = &domain;
		}
		disk_options.limits = {pressure ? 1056ull : 512ull * 1024 * 1024, paged ? 4096u : 100u, 10};
		std::optional<l::ReadView> pinned_flat;
		if (migration) {
			const auto reply = response(count);
			d::Limits input_limits;
			input_limits.storage_account = account;
			const std::array<std::string_view, 1> names{"app"};
			t::ResponseDecoder decoder(
				cursor(), {},
				d::NamespaceSelection(count ? std::span(names) : std::span<const std::string_view>{}), false,
				input_limits);
			auto bytes = std::span<const char>(reply);
			while (!decoder.done()) {
				d::WorkBudget budget;
				if (!bytes.empty() && decoder.needs_input())
					decoder.feed(bytes, budget);
				else
					decoder.step(budget);
			}
			auto input = decoder.take();
			l::GenerationId initial{};
			initial.fill('i');
			l::ImageBuilder builder(input.snapshot, initial);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			auto write = [&](const auto &path, std::span<const char> contents) {
				std::ofstream file(path, std::ios::binary);
				file.write(contents.data(), contents.size());
				file.close();
				check(bool(file));
				std::filesystem::permissions(path, std::filesystem::perms::owner_read |
													   std::filesystem::perms::owner_write);
			};
			const auto name = l::generation_name(initial);
			write(directory / (name + ".image"), builder.bytes()->view());
			write(directory / (name + ".lease"), {});
			write(directory / "owner.lock", {});
			write(directory / "selected", l::encode_descriptor(builder.descriptor()));
			pinned_flat = l::recover_generation(directory, disk_options.recovery_limits).view();
			disk_options.mode = l::StoreOpenMode::UpgradeLegacy;
		}
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), disk_options);
		checkpoint("legacy-image-ready");
		disk_options.mode = l::StoreOpenMode::FreshEmpty;
		auto server_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*server_tls,
								{(certificates / "one.pem").string(), (certificates / "one.key").string(),
								 (certificates / "ca.pem").string()},
								reactor::tls::Role::Server);
		auto client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*client_tls, {"", "", (certificates / "ca.pem").string()},
								reactor::tls::Role::Client);
		asio::io_context server_io;
		asio::ip::tcp::acceptor acceptor(server_io, {asio::ip::address_v4::loopback(), 0});
		std::exception_ptr server_failure;

		asio::io_context io(1);
		asio::thread_pool worker(1);
		auto queue = kronuz::io::completion::make_native_queue(
			kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
		auto driver = std::allocate_shared<t::NativeDriver>(d::StorageAllocator<t::NativeDriver>(account),
															io.get_executor(), queue);
		t::CoordinatorOptions options;
		options.directory = directory;
		options.image_format = paged ? t::LocalImageFormat::PagedV2 : t::LocalImageFormat::FlatV1;
		options.delivery_mode = subscription ? t::DeliveryMode::Subscription : t::DeliveryMode::Poll;
		options.poll_interval =
			(cancel_selected || pressure || reset_retry)
				? 10ms
				: std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(12));
		options.source.endpoint = acceptor.local_endpoint();
		options.source.hostname = "localhost";
		options.source.token = "0123456789abcdef0123456789abcdef";
		options.source.tls_context = client_tls;
		options.source.configured = cursor();
		options.source.compression = false;
		if (poll_backoff) {
			options.poll_interval = 20ms;
			options.reconnect_maximum = 80ms;
			options.reconnect_seed = 17;
			options.authority_endpoints = {options.source.endpoint};
			options.source.timeout = 10s;
		}
		if (paged && count >= 500000)
			options.source.timeout = std::chrono::seconds(240);
		else if (count >= 500000)
			options.source.timeout = std::chrono::seconds(120);
#ifdef DETENT_TEST_RELAY_HTTP
		if (relay_network)
			options.source.timeout = std::chrono::seconds(240);
#endif
		if (count)
			options.source.namespaces = {"app"};
		options.view_limits.mapping_account = mapping;
		options.view_limits.retirement_domain = &domain;
		std::shared_ptr<RecordingSink> recording;
		if (sink) {
			recording = std::allocate_shared<RecordingSink>(d::StorageAllocator<RecordingSink>(account));
			options.publication_sink = recording;
		}
		auto cache_account = std::make_shared<d::StorageLedger>(root, 64 * 1024 * 1024);
		auto control_account = std::make_shared<d::StorageLedger>(root,
#ifdef DETENT_TEST_RELAY_HTTP
																  (relay_network ? 64 : 16) * 1024 * 1024
#else
																  16 * 1024 * 1024
#endif
		);
		std::optional<d::RetirementDomain> cache_domain;
		std::shared_ptr<detent::relay::Bundle> relay_bundle;
		d::StorageLedger::Ticket cache_pressure;
		if (relay) {
			cache_domain.emplace(runtime.domain(cache_account, 64 + 500));
			detent::relay::BundleOptions bundle_options;
			bundle_options.configured = cursor();
			bundle_options.namespaces = count ? 1 : 0;
			bundle_options.incarnation.fill('r');
			relay_bundle = detent::relay::make_bundle(bundle_options, *cache_domain);
			check(!relay_bundle->status().ready);
			options.publication_sink = relay_bundle;
			if (relay_pressure)
				cache_pressure = cache_account->admit(cache_account->limit() - cache_account->used());
		}
		auto coordinator = t::make_coordinator(options, store, driver, domain, worker);
		{
			auto quota = std::make_shared<d::StorageLedger>(65536);
			auto quota_domain = runtime.domain(quota, 8);
			auto pressure = quota->admit(quota->limit() - quota->used());
			bool refused = false;
			try {
				t::prepare_coordinator(options, driver, quota_domain, worker);
			} catch (const d::StoragePressure &) {
				refused = true;
			}
			check(refused);
			quota_domain.close().wait();
		}
		acceptor.non_blocking(true);
		std::jthread server([&] {
			try {
				for (unsigned attempt = 0; attempt != (maintenance_failure ? 0u
													   : poll_backoff	   ? 5u
													   : cancel_open	   ? 2u
													   : reset_retry	   ? 11u
													   : worker_cancel	   ? 3u
													   : subscription	   ? (transitions ? 6u : 4u)
													   : pressure		   ? 3u
													   : cancel_selected   ? 2u
																		   : 1u);
					 ++attempt) {
					const auto phase =
						cancel_open		? std::array<unsigned, 2>{0, 3}[attempt]
						: worker_cancel ? std::array<unsigned, 3>{0, 1, 3}[attempt]
						: reset_retry	? std::array<unsigned, 11>{0, 1, 4, 3, 0, 5, 3, 0, 1, 2, 3}[attempt]
										: attempt;
					asio::ip::tcp::socket accepted(server_io);
					const auto accept_deadline = std::chrono::steady_clock::now() + options.source.timeout;
					for (;;) {
						asio::error_code error;
						acceptor.accept(accepted, error);
						if (!error)
							break;
						if (error != asio::error::would_block && error != asio::error::try_again)
							throw asio::system_error(error);
						if (std::chrono::steady_clock::now() >= accept_deadline)
							throw std::runtime_error(
								"coordinator accept deadline: records=" + std::to_string(count) +
								" attempt=" + std::to_string(attempt) + " phase=" + std::to_string(phase));
						std::this_thread::sleep_for(1ms);
					}
					asio::ssl::stream<asio::ip::tcp::socket> socket(std::move(accepted), *server_tls);
					checkpoint("server-accepted");
					socket.handshake(asio::ssl::stream_base::server);
					checkpoint("server-handshake");
					asio::streambuf request;
					asio::read_until(socket, request, "\r\n\r\n");
					if (cancel_selected || pressure || subscription || poll_backoff) {
						auto text = [&] {
							return std::string(asio::buffers_begin(request.data()),
											   asio::buffers_end(request.data()));
						};
						auto header = text();
						if (cancel_open)
							check(header.starts_with(phase == 0 ? "POST /v2/distribution/session/open "
																: "POST /v2/distribution/session/close "));
						const auto body_start = header.find("\r\n\r\n") + 4;
						const auto length = std::stoul(header.substr(header.find("Content-Length: ") + 16));
						if (request.size() < body_start + length)
							asio::read(socket, request,
									   asio::transfer_exactly(body_start + length - request.size()));
						const auto body = text().substr(body_start);
						if (poll_backoff) {
							check(header.starts_with("POST /v2/distribution ") &&
								  (body.find("\"base\":{") != std::string::npos) == (attempt == 4));
							if (attempt >= 3) {
								std::promise<bool> observed;
								auto ready = observed.get_future();
								asio::post(io, [&] {
									const auto retry =
										t::detail::CoordinatorAccess::reconnect_observation(*coordinator);
									const auto state = coordinator->status();
									bool valid =
										retry.count == 3 && retry.delay >= 40ms && retry.delay <= 80ms;
									if (attempt == 3)
										valid = valid && !state.selected.descriptor && !state.received;
									else
										valid = valid && state.acknowledged == cursor() &&
												state.recoverable == cursor() && state.selected.descriptor &&
												state.selected.descriptor->cursor == cursor();
									observed.set_value(valid);
								});
								check(ready.get());
							}
							const std::string unavailable = "{\"error\":\"outcome_unknown\"}";
							auto bytes = attempt == 3 ? response(count)
													  : "HTTP/1.1 503 Service Unavailable\r\n"
														"Content-Type: application/json\r\nContent-Length: " +
															std::to_string(unavailable.size()) + "\r\n\r\n" +
															unavailable;
							bytes.insert(bytes.find("\r\n") + 2, "Connection: close\r\n");
							asio::write(socket, asio::buffer(bytes));
							asio::error_code ignored;
							socket.lowest_layer().close(ignored);
							if (attempt == 4)
								feedback_received.store(true, std::memory_order_release);
							continue;
						}
						if (reset_retry && (attempt == 4 || attempt == 7))
							check(body.find("\"force_snapshot\":true") != std::string::npos);
						if (!subscription)
							check((body.find("\"base\":{") != std::string::npos) == (attempt != 0));
						if (subscription && phase == 2) {
							if (body.find("\"after\":1") == std::string::npos ||
								body.find("\"published\":{") == std::string::npos ||
								body.find("\"revision\":1") == std::string::npos)
								throw std::runtime_error("coordinator publication feedback: " + body);
							std::promise<bool> recovered;
							auto ready = recovered.get_future();
							asio::post(io, [&] {
								try {
									const auto status = coordinator->status();
									bool valid = status.recoverable == cursor() &&
												 status.selected.descriptor &&
												 status.selected.descriptor->cursor == cursor();
									if (paged)
										valid = valid &&
												status.selected.descriptor->length == l::paged::root_bytes;
									if (pinned_flat)
										valid = valid && pinned_flat->cursor() == cursor() &&
												pinned_flat->size() == count;
									if (relay_bundle) {
										valid = valid && relay_bundle->status().ready;
										if (relay_pressure) {
											valid = valid && relay_bundle->status().refusals > 0 &&
													relay_bundle->status().gaps > 0;
											cache_pressure.reset();
											unsigned turns = 0;
											for (;;) {
												d::WorkBudget budget;
												if (relay_bundle->snapshot_step(budget))
													break;
												if (++turns == 1000) {
													valid = false;
													break;
												}
											}
										}
										auto offer = relay_bundle->offer({}, true);
										valid = valid && offer.decision == d::CacheDecision::Update &&
												offer.update && offer.update->target() == cursor();
										if (valid) {
											for (bool compressed : {false, true}) {
												d::FragmentDecoder fragments(d::UpdateKind::Snapshot,
																			 offer.update->logical_bytes(),
																			 compressed);
												std::string payload;
												for (std::size_t ordinal = 0;
													 ordinal < offer.update->fragments(); ++ordinal) {
													auto frame = offer.update->frame(ordinal, compressed);
													auto bytes =
														std::span(frame->view().data(), frame->size());
													while (!bytes.empty()) {
														d::WorkBudget budget;
														fragments.feed(bytes, budget);
														if (auto piece = fragments.take())
															payload.append(piece->bytes());
													}
												}
												fragments.finish();
												d::SnapshotDecoder decoder(
													cursor(), count ? std::vector<std::string>{"app"}
																	: std::vector<std::string>{});
												auto bytes =
													std::span<const char>(payload.data(), payload.size());
												while (!bytes.empty()) {
													d::WorkBudget budget;
													decoder.feed(bytes, budget);
												}
												valid = valid && decoder.finish().records().size() == count;
											}
										}
									}
#ifdef DETENT_TEST_RELAY_HTTP
									if (valid && relay_network) {
										auto offer = relay_bundle->offer({}, true);
										asio::co_spawn(
											io,
											relay_network_fixture::qualify(
												relay_bundle, std::move(offer.update), control_account,
												server_tls, client_tls, worker, count,
												network_failures ? relay_network_fixture::FailurePoint::Suite
																 : relay_network_fixture::FailurePoint::None),
											[&recovered](std::exception_ptr error) {
												if (error)
													recovered.set_exception(error);
												else
													recovered.set_value(true);
											});
										return;
									}
									if (valid && relay_http) {
										auto offer = relay_bundle->offer({}, true);
										relay_service_fixture::qualify(relay_bundle, std::move(offer.update),
																	   control_account, count != 0);
									}
#endif
									if (valid && relay_sessions) {
										auto offer = relay_bundle->offer({}, true);
										qualify_relay_sessions(relay_bundle, std::move(offer.update),
															   control_account, cache_account);
									}

									recovered.set_value(valid);
								} catch (...) {
									recovered.set_exception(std::current_exception());
								}
							});
							check(ready.get());
							if (transitions)
								asio::post(io, [&] {
									if (compaction_pressure)
										t::detail::CoordinatorAccess::fail_next_compaction_admission(
											*coordinator);
									t::detail::CoordinatorAccess::request_compaction(*coordinator);
								});
							else
								feedback_received.store(true);
						}
						if (transitions && (phase == 3 || phase == 4)) {
							if (body.find("\"after\":" + std::to_string(phase - 1)) == std::string::npos) {
								std::promise<std::string> diagnostic;
								auto result = diagnostic.get_future();
								asio::post(io, [&] {
									try {
										if (auto error = coordinator->status().error)
											std::rethrow_exception(error);
										diagnostic.set_value("no coordinator error");
									} catch (const std::exception &error) {
										diagnostic.set_value(error.what());
									}
								});
								throw std::runtime_error("transition feedback: " + body + "; " +
														 result.get());
							}
							check(body.find("\"after\":" + std::to_string(phase - 1)) != std::string::npos);
							check(body.find("\"revision\":" + std::to_string(phase - 1)) !=
								  std::string::npos);
							std::promise<bool> verified;
							auto ready = verified.get_future();
							asio::post(io, [&] {
								const auto status = coordinator->status();
								auto expected = cursor();
								expected.revision = phase - 1;
								verified.set_value(
									status.recoverable == expected && status.acknowledged == expected &&
									status.selected.descriptor &&
									status.selected.descriptor->cursor == expected &&
									status.selected.descriptor->length == l::paged::root_bytes &&
									t::detail::CoordinatorAccess::completed_compactions(*coordinator) ==
										(compaction_pressure ? 0u : 1u));
							});
							check(ready.get());
							if (phase == 4)
								feedback_received.store(true);
						}
						if (attempt && !subscription)
							check(body.find("\"revision\":" + std::to_string(pressure ? attempt : 1)) !=
								  std::string::npos);
					}
					auto bytes = response(count, pressure ? attempt + 1 : 1);
					if (subscription) {
						if (transitions && (phase == 2 || phase == 3)) {
							bytes = transition_response(phase == 2);
							bytes.insert(bytes.find("\r\n") + 2,
										 "X-Detent-Session-Version: 1\r\nX-Detent-Session: "
										 "11111111111111111111111111111111\r\nX-Detent-Window: " +
											 std::to_string(phase) + "\r\n");
						} else if (transitions && phase == 4) {
							std::array<char, 1> buffer;
							asio::error_code error;
							socket.read_some(asio::buffer(buffer), error);
							check(bool(error));
							continue;
						} else if (phase == 0) {
							const std::string body =
								"{\"version\":1,\"session\":\"11111111111111111111111111111111\","
								"\"window\":0,\"mode\":\"snapshot\",\"wait_ms\":30000}";
							bytes = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
									std::to_string(body.size()) + "\r\n\r\n" + body;
						} else if (phase == 1 || phase == 5) {
							bytes.insert(bytes.find("\r\n") + 2,
										 "X-Detent-Session-Version: 1\r\nX-Detent-Session: "
										 "11111111111111111111111111111111\r\n"
										 "X-Detent-Window: 1\r\n");
							if (phase == 5)
								bytes.resize(bytes.find("\r\n\r\n") + 4 + 48);
						} else if (phase == 4) {
							const std::string body =
								"{\"error\":\"snapshot_required\",\"reason\":\"history_lost\"}";
							bytes = "HTTP/1.1 409 Conflict\r\nContent-Type: "
									"application/json\r\nContent-Length: " +
									std::to_string(body.size()) + "\r\n\r\n" + body;
						} else if (phase == 2) {
							// Hold an original long poll until owner cancellation closes the socket.
							std::array<char, 1> input;
							asio::error_code error;
							socket.read_some(asio::buffer(input), error);
							check(bool(error));
							continue;
						} else {
							bytes = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
						}
					}
					if (!retained_idle)
						bytes.insert(bytes.find("\r\n") + 2, "Connection: close\r\n");
					asio::write(socket, asio::buffer(bytes));
					if (retained_idle) {
						pollfd descriptor{socket.next_layer().native_handle(), POLLIN, 0};
						check(::poll(&descriptor, 1, 5000) > 0);
						std::array<char, 1> byte;
						asio::error_code error;
						socket.read_some(asio::buffer(byte), error);
						check(bool(error));
					}
				}
			} catch (...) {
				server_failure = std::current_exception();
				asio::post(io, [&] { coordinator->close(); });
			}
		});
		if (cancel_open)
			t::detail::CoordinatorAccess::close_after_next_session_open(*coordinator);
		if (maintenance_failure)
			t::detail::CoordinatorAccess::fail_next_maintenance_recovery(*coordinator,
																		 original_maintenance_failure);
		Timing timing;
		auto done = asio::co_spawn(io,
								   exercise(coordinator, timing, cancel_selected, pressure, options,
											(subscription || poll_backoff) ? &feedback_received : nullptr,
											cancel_open, transitions, worker_cancel, maintenance_failure,
											poll_backoff),
								   asio::use_future);
		checkpoint("coordinator-running");
		io.run();
		checkpoint("coordinator-settled");
		std::exception_ptr run_failure;
		try {
			done.get();
		} catch (...) {
			run_failure = std::current_exception();
		}
		server.join();
		checkpoint("server-joined");
		if (server_failure || run_failure) {
			auto report = [](const char *label, std::exception_ptr failure) {
				if (!failure)
					return;
				try {
					std::rethrow_exception(failure);
				} catch (const std::exception &error) {
					std::cerr << label << ": " << error.what() << '\n';
				}
			};
			report("fixture server", server_failure);
			report("fixture coordinator", run_failure);
		}
		if (server_failure)
			std::rethrow_exception(server_failure);
		if (run_failure)
			std::rethrow_exception(run_failure);
		auto final_cursor = cursor();
		if (pressure || transitions)
			final_cursor.revision = 3;
		check(coordinator->drained());
		if (poll_backoff)
			check(coordinator->status().phase == t::CoordinatorPhase::Closed &&
				  !coordinator->status().worker_pending);
		check(!t::detail::CoordinatorAccess::retains_paged_candidate(*coordinator));
		if (worker_cancel)
			check(t::detail::CoordinatorAccess::cancelled_construction_settled(*coordinator));
		if (relay_bundle)
			check(relay_bundle->status().fenced && !relay_bundle->status().ready);
		if (recording) {
			check(recording->fenced && recording->stages > 0 && recording->commits > 0);
			check(recording->last_selected == store->selection().descriptor);
			check(recording->authentications == (subscription ? 1u : 0u));
		}
		if (cancel_open || worker_cancel) {
			check(!store->selection().descriptor && (worker_cancel || !coordinator->status().received) &&
				  !coordinator->status().acknowledged);
		} else if (maintenance_failure) {
			check(coordinator->status().error == original_maintenance_failure &&
				  !coordinator->status().acknowledged);
		} else {
			check(store->selection().descriptor->cursor == final_cursor);
			check(coordinator->status().received == final_cursor &&
				  coordinator->status().validated == final_cursor);
		}
		// run() retains the facade before its coroutine starts. A close issued
		// before first execution still drives startup cancellation to drainage.
		auto early_directory = directory / "early-close";
		std::filesystem::create_directory(early_directory);
		std::filesystem::permissions(early_directory, std::filesystem::perms::owner_all);
		auto early_store =
			l::make_disk_store(domain, early_directory, std::make_shared<int>(1), disk_options);
		auto early_options = options;
		early_options.directory = early_directory;
		early_options.publication_sink.reset();
		auto early = t::make_coordinator(early_options, early_store, driver, domain, worker);
		auto original = early->run();
		early->close();
		std::weak_ptr<t::Coordinator> weak = early;
		early.reset();
		check(!weak.expired());
		io.restart();
		auto early_done = asio::co_spawn(io, std::move(original), asio::use_future);
		io.run();
		early_done.get();
		check(weak.expired() && early_store->drained());
		early_store.reset();
		checkpoint("early-close-settled");
		auto failed_directory = directory / "failed-startup";
		std::filesystem::create_directory(failed_directory);
		std::filesystem::permissions(failed_directory, std::filesystem::perms::owner_all);
		auto failed_store =
			l::make_disk_store(domain, failed_directory, std::make_shared<int>(1), disk_options);
		auto failed_original = failed_store->operation();
		check(static_cast<bool>(failed_original));
		failed_original->submitted();
		auto completion =
			kronuz::journal::detail::execute_primitive(failed_original->io(), failed_original->request());
		completion.error = std::make_exception_ptr(std::runtime_error("injected startup original failure"));
		check(failed_original->complete(std::move(completion)));
		failed_original.reset();
		check(failed_store->status() == l::StoreStatus::Fenced);
		auto failed_options = options;
		failed_options.directory = failed_directory;
		failed_options.publication_sink.reset();
		auto failed = t::make_coordinator(failed_options, failed_store, driver, domain, worker);
		io.restart();
		auto failed_done = asio::co_spawn(io, failed->run(), asio::use_future);
		io.run();
		failed_done.get();
		check(failed->drained() && failed->status().phase == t::CoordinatorPhase::Fenced &&
			  failed->status().error);
		failed.reset();
		failed_store.reset();
		checkpoint("failed-startup-settled");
		auto unrelated_resident = account->admit(64 * 1024);
		auto pressure_account =
			std::make_shared<d::StorageLedger>(account, account->limit() - account->used());
		auto pressure_domain = runtime.domain(pressure_account, 64);
		auto pressure_disk_options = disk_options;
		pressure_disk_options.recovery_limits.retirement_domain = &pressure_domain;
		auto pressure_directory = directory / "cache-admission-pressure";
		std::filesystem::create_directory(pressure_directory);
		std::filesystem::permissions(pressure_directory, std::filesystem::perms::owner_all);
		auto pressure_store =
			l::make_disk_store(pressure_domain, pressure_directory, std::make_shared<int>(1), pressure_disk_options);
		for (unsigned turn = 0; pressure_store->operation() || pressure_store->needs_work(); ++turn) {
			check(turn < 10000);
			if (auto operation = pressure_store->operation()) {
				operation->submitted();
				check(operation->complete(
					kronuz::journal::detail::execute_primitive(operation->io(), operation->request())));
			}
			pressure_store->advance();
		}
		check(pressure_store->status() == l::StoreStatus::Ready);
		auto pressure_options = options;
		pressure_options.directory = pressure_directory;
		pressure_options.publication_sink.reset();
		auto pressured = t::make_coordinator(pressure_options, pressure_store, driver, pressure_domain, worker);
		auto exhausted = pressure_account->admit(pressure_account->limit() - pressure_account->used());
		// An unrelated refund must not undo the injected admission refusal.
		unrelated_resident.reset();
		checkpoint("pressure-startup-running");
		io.restart();
		auto pressure_done = asio::co_spawn(io, pressured->run(), asio::use_future);
		io.run();
		pressure_done.get();
		check(pressured->drained() && pressured->status().phase == t::CoordinatorPhase::Fenced &&
			  pressured->status().error);
		bool admission_refused = false;
		try {
			std::rethrow_exception(pressured->status().error);
		} catch (const d::StoragePressure &) {
			admission_refused = true;
		}
		check(admission_refused && pressure_store->drained());
		exhausted.reset();
		pressured.reset();
		pressure_store.reset();
		pressure_domain.close().wait();
		checkpoint("pressure-startup-settled");
		const auto disk_peak = store->stats().peak.bytes;
		checkpoint("worker-draining");
		worker.join();
		coordinator.reset();
		options.publication_sink.reset();
		relay_bundle.reset();
		if (cache_domain) {
			cache_domain->close().wait();
			cache_domain.reset();
		}

		driver.reset();
		queue.reset();
		store.reset();
		pinned_flat.reset();
		checkpoint("domain-draining");
		domain.close().wait();
		checkpoint("domain-drained");
		runtime.shutdown();
		check(cache_account->used() == 0);
		if (!cancel_open && !worker_cancel) {
			auto recovered = l::recover_generation(directory);
			check(recovered.descriptor().cursor == final_cursor);
			check(recovered.view().size() == count);
			const auto view = recovered.view();
			check(view.int_count() == (count + 3) / 4 && view.bool_count() == count - (count + 3) / 4);
			for (std::uint32_t i = 0; i != count; ++i) {
				char digits[7];
				const auto end = std::to_chars(digits, digits + sizeof(digits), i).ptr;
				std::string key = "key";
				key.append(6 - (end - digits), '0');
				key.append(digits, end);
				if (i % 4 == 0) {
					auto integer = view.try_get("app", key);
					check(integer && *integer && integer->value() == -std::int64_t(i));
				} else {
					auto boolean = view.try_eval("app", key);
					check(boolean && *boolean && boolean->value() == bool(i % 2));
					auto switched = view.try_eval("app", key, {}, "beta");
					check(switched && *switched && switched->value() == bool(i % 2));
					auto cohort = view.try_eval("app", key, "scale-host", "beta");
					check(cohort && *cohort &&
						  cohort->value() == (detent::stable_bucket("app:" + key, "scale-host") < 17));
				}
			}
		}
		check(mapping->used() == 0);
		std::cout << "{\"records\":" << count << ",\"managed_heap_peak\":" << account->peak()
				  << ",\"cache_heap_peak\":" << cache_account->peak()
				  << ",\"control_heap_peak\":" << control_account->peak()
				  << ",\"combined_heap_peak\":" << root->peak() << ",\"mapped_peak\":" << mapping->peak()
				  << ",\"disk_peak\":" << disk_peak << ",\"timer_turns\":" << timing.turns
				  << ",\"maximum_timer_lateness_us\":" << timing.maximum_lateness_us << ",\"wall_ms\":"
				  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
																		   started)
						 .count()
				  << "}\n";
		std::filesystem::remove_all(directory);
		std::cout << "TLS receive, native publication, acknowledgement and coordinator drainage passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
