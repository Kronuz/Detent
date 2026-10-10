#include "../../src/detent_delivery/coordinator_internal.hh"
#include "detent_delivery/coordinator.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_distribution/messagepack.hh"
#include <asio/experimental/awaitable_operators.hpp>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace t = detent::delivery;
namespace m = d::messagepack;
using namespace std::chrono_literals;
void check(bool value) {
	if (!value)
		throw std::runtime_error("coordinator invariant");
}
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
struct Timing {
	std::uint64_t turns = 0;
	std::int64_t maximum_lateness_us = 0;
};
asio::awaitable<void> observe(std::shared_ptr<t::Coordinator> coordinator, Timing &timing,
							  bool cancel_selected, const std::atomic<bool> *feedback_received = nullptr) {
	auto executor = co_await asio::this_coro::executor;
	asio::steady_timer timer(executor);
	const auto deadline = std::chrono::steady_clock::now() + 60s;
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
		while (!feedback_received->load() && std::chrono::steady_clock::now() < deadline) {
			timer.expires_after(1ms);
			co_await timer.async_wait(asio::use_awaitable);
		}
		if (!feedback_received->load())
			coordinator->close();
		check(feedback_received->load());
	}
	// Close wakes the parked candidate after the source observes durable feedback.
	timer.expires_after(5ms);
	co_await timer.async_wait(asio::use_awaitable);
	coordinator->close();
	check(published && coordinator->status().recoverable == cursor());
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
							   const std::atomic<bool> *feedback_received, bool cancel_open = false) {
	using namespace asio::experimental::awaitable_operators;
	if (cancel_open) {
		co_await coordinator->run();
		co_return;
	}
	if (pressure) {
		co_await (coordinator->run() && observe_pressure(coordinator, options));
		co_return;
	}
	co_await (coordinator->run() && observe(coordinator, timing, cancel_selected, feedback_received));
}
int main(int argc, char **argv) {
	try {
		check(argc == 2 || argc == 3);
		const bool sink = argc == 3 && std::string_view(argv[2]).find("sink") != std::string_view::npos;
		const bool cancel_selected = argc == 3 && (std::string_view(argv[2]) == "--cancel-selected" ||
												   std::string_view(argv[2]) == "--sink-cancel-selected");
		const bool pressure = argc == 3 && std::string_view(argv[2]) == "--reader-pressure";
		const bool cancel_open = argc == 3 && std::string_view(argv[2]) == "--subscription-cancel-open";
		const bool reset_retry = argc == 3 && std::string_view(argv[2]) == "--subscription-reset";
		const bool subscription = argc == 3 && std::string_view(argv[2]).starts_with("--subscription");
		const auto requested = subscription
								   ? (std::string_view(argv[2]) == "--subscription-500000" ? 500000u : 0u)
							   : argc == 3 && !cancel_selected && !pressure && !sink ? std::stoul(argv[2])
																					 : 0;
		std::atomic<bool> feedback_received{false};
		check(requested <= 500000);
		const auto count = static_cast<std::uint32_t>(requested);
		const auto started = std::chrono::steady_clock::now();
		const std::filesystem::path certificates(argv[1]);
		auto directory = std::filesystem::current_path() / ".scratch" /
						 ("delivery-coordinator-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto account = std::make_shared<d::StorageLedger>(256 * 1024 * 1024);
		d::RetirementRuntime runtime(account, 2);
		auto domain = runtime.domain(account, 64);
		l::DiskStoreOptions disk_options;
		disk_options.mode = l::StoreOpenMode::FreshEmpty;
		disk_options.limits = {pressure ? 1056ull : 512ull * 1024 * 1024, 100, 10};
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), disk_options);
		auto server_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		detent::transport::configure_tls(*server_tls,
										 {(certificates / "one.pem").string(),
										  (certificates / "one.key").string(),
										  (certificates / "ca.pem").string()},
										 detent::transport::TlsRole::PublisherServer);
		auto client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		detent::transport::configure_tls(*client_tls, {"", "", (certificates / "ca.pem").string()},
										 detent::transport::TlsRole::PublisherClient);
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
		if (count)
			options.source.namespaces = {"app"};
		auto mapping = std::make_shared<d::StorageLedger>(512ull * 1024 * 1024);
		options.view_limits.mapping_account = mapping;
		options.view_limits.retirement_domain = &domain;
		std::shared_ptr<RecordingSink> recording;
		if (sink) {
			recording = std::allocate_shared<RecordingSink>(d::StorageAllocator<RecordingSink>(account));
			options.publication_sink = recording;
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
				for (unsigned attempt = 0; attempt != (cancel_open		 ? 2u
													   : reset_retry	 ? 11u
													   : subscription	 ? 4u
													   : pressure		 ? 3u
													   : cancel_selected ? 2u
																		 : 1u);
					 ++attempt) {
					const auto phase =
						cancel_open	  ? std::array<unsigned, 2>{0, 3}[attempt]
						: reset_retry ? std::array<unsigned, 11>{0, 1, 4, 3, 0, 5, 3, 0, 1, 2, 3}[attempt]
									  : attempt;
					asio::ip::tcp::socket accepted(server_io);
					const auto accept_deadline = std::chrono::steady_clock::now() + 60s;
					for (;;) {
						asio::error_code error;
						acceptor.accept(accepted, error);
						if (!error)
							break;
						if ((error != asio::error::would_block && error != asio::error::try_again) ||
							std::chrono::steady_clock::now() >= accept_deadline)
							throw asio::system_error(error);
						std::this_thread::sleep_for(1ms);
					}
					asio::ssl::stream<asio::ip::tcp::socket> socket(std::move(accepted), *server_tls);
					socket.handshake(asio::ssl::stream_base::server);
					asio::streambuf request;
					asio::read_until(socket, request, "\r\n\r\n");
					if (cancel_selected || pressure || subscription) {
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
						if (reset_retry && (attempt == 4 || attempt == 7))
							check(body.find("\"force_snapshot\":true") != std::string::npos);
						if (!subscription)
							check((body.find("\"base\":{") != std::string::npos) == (attempt != 0));
						if (subscription && phase == 2) {
							check(body.find("\"after\":1") != std::string::npos &&
								  body.find("\"published\":{") != std::string::npos &&
								  body.find("\"revision\":1") != std::string::npos);
							std::promise<bool> recovered;
							auto ready = recovered.get_future();
							asio::post(io, [&] {
								const auto status = coordinator->status();
								recovered.set_value(status.recoverable == cursor() &&
													status.selected.descriptor &&
													status.selected.descriptor->cursor == cursor());
							});
							check(ready.get());
							feedback_received.store(true);
						}
						if (attempt && !subscription)
							check(body.find("\"revision\":" + std::to_string(pressure ? attempt : 1)) !=
								  std::string::npos);
					}
					auto bytes = response(count, pressure ? attempt + 1 : 1);
					if (subscription) {
						if (phase == 0) {
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
					asio::write(socket, asio::buffer(bytes));
				}
			} catch (...) {
				server_failure = std::current_exception();
			}
		});
		if (cancel_open)
			t::detail::CoordinatorAccess::close_after_next_session_open(*coordinator);
		Timing timing;
		auto done = asio::co_spawn(io,
								   exercise(coordinator, timing, cancel_selected, pressure, options,
											subscription ? &feedback_received : nullptr, cancel_open),
								   asio::use_future);
		io.run();
		done.get();
		server.join();
		if (server_failure)
			std::rethrow_exception(server_failure);
		auto final_cursor = cursor();
		if (pressure)
			final_cursor.revision = 3;
		check(coordinator->drained());
		if (recording) {
			check(recording->fenced && recording->stages > 0 && recording->commits > 0);
			check(recording->last_selected == store->selection().descriptor);
			check(recording->authentications == (subscription ? 1u : 0u));
		}
		if (cancel_open) {
			check(!store->selection().descriptor && !coordinator->status().received &&
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
		const auto disk_peak = store->stats().peak.bytes;
		worker.join();
		coordinator.reset();
		driver.reset();
		queue.reset();
		store.reset();
		domain.close().wait();
		if (!cancel_open) {
			auto recovered = l::recover_generation(directory);
			check(recovered.descriptor().cursor == final_cursor);
			check(recovered.view().size() == count);
			if (count > 4) {
				auto integer = recovered.view().try_get("app", "key000004");
				check(integer && *integer && integer->value() == -4);
				auto boolean = recovered.view().try_eval("app", "key000001", {}, "beta");
				check(boolean && *boolean);
			}
		}
		check(mapping->used() == 0);
		std::cout << "{\"records\":" << count << ",\"managed_heap_peak\":" << account->peak()
				  << ",\"mapped_peak\":" << mapping->peak() << ",\"disk_peak\":" << disk_peak
				  << ",\"timer_turns\":" << timing.turns
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
