#pragma once
// A scripted upstream for coordinator tests: a TLS server on its own thread that answers the session endpoint
// with a snapshot of `knobs` knobs in ten namespaces and, on request, the delta from revision 1 to 2 on n3.
#include "../../src/detent_delivery/coordinator_internal.hh"
#include "detent_delivery/coordinator.hh"
#include "detent_distribution/framing.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_relay/bundle.hh"
#include "leased_paged_root.hh"
#include "prepared_reader.hh"
#include "recovery_context.hh"
#include "task_support.hh"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <source_location>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace scripted_upstream {
namespace d = detent::distribution;
namespace l = detent::local;
namespace t = detent::delivery;
namespace x = l::detail;
namespace m = d::messagepack;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

inline int knobs = std::getenv("DETENT_REPAIR_KNOBS") ? std::atoi(std::getenv("DETENT_REPAIR_KNOBS")) : 100000;
inline constexpr int namespaces = 10;
inline std::atomic<char> fixture_epoch{'e'};

inline d::Cursor cursor(std::uint64_t revision = 1) {
	d::Cursor value;
	value.cluster.fill('c');
	value.group.fill('g');
	value.restore_epoch.fill(fixture_epoch.load(std::memory_order_relaxed));
	value.scope.fill('s');
	value.revision = revision;
	return value;
}
inline std::string space(int i) { return "n" + std::to_string(i % namespaces); }
inline std::string name(int i) { return space(i) + ":k" + std::to_string(i); }
// Revision 2 is revision 1 after a delta on namespace n3: every 500th knob removed, every 50th bumped.
inline std::optional<std::int64_t> expected(unsigned revision, int i) {
	const std::int64_t base = std::int64_t(i) * 3 + 1;
	if (revision < 2 || i % namespaces != 3)
		return base;
	if (i % 500 == 3)
		return {};
	return i % 50 == 3 ? base + 1000 : base;
}
inline void delta_changes(std::map<std::string, std::int64_t> &upserts, std::vector<std::string> &removals) {
	for (int i = 3; i < knobs; i += namespaces) {
		if (i % 500 == 3)
			removals.push_back(name(i));
		else if (i % 50 == 3)
			upserts[name(i)] = *expected(2, i);
	}
}
// The sorted (namespace, name) order the wire wants.
inline std::vector<int> ordered(unsigned revision) {
	std::vector<int> order;
	for (int i = 0; i < knobs; ++i)
		if (expected(revision, i))
			order.push_back(i);
	std::sort(order.begin(), order.end(), [](int a, int b) {
		return std::pair(a % namespaces, name(a)) < std::pair(b % namespaces, name(b));
	});
	return order;
}
inline void put_cursor(std::string &out, const d::Cursor &value) {
	m::array(out, 5);
	for (const auto *field : {&value.cluster, &value.group, &value.restore_epoch})
		m::string(out, std::string_view(field->data(), field->size()), true);
	m::string(out, std::string_view(value.scope.data(), value.scope.size()), true);
	m::unsigned_integer(out, value.revision);
}
inline std::string http(std::string_view headers, const std::string &body) {
	return "HTTP/1.1 200 OK\r\n" + std::string(headers) + "Connection: close\r\nContent-Length: " +
		   std::to_string(body.size()) + "\r\n\r\n" + body;
}
inline std::string json_reply(const std::string &body) { return http("Content-Type: application/json\r\n", body); }
inline std::string window_headers(std::uint64_t window) {
	return "X-Detent-Session-Version: 1\r\nX-Detent-Session: 11111111111111111111111111111111\r\nX-Detent-Window: " +
		   std::to_string(window) + "\r\n";
}
// The whole store at `revision` as one snapshot response of a session window.
inline std::string snapshot_reply(unsigned revision, std::uint64_t window) {
	const auto order = ordered(revision);
	std::uint64_t strings = 0;
	for (const int i : order)
		strings += name(i).size();
	for (int ns = 0; ns < namespaces; ++ns)
		strings += space(ns).size();
	std::string wire;
	m::array(wire, 7);
	m::unsigned_integer(wire, 1);
	m::unsigned_integer(wire, 1);
	m::array(wire, 2);
	m::unsigned_integer(wire, 1);
	m::unsigned_integer(wire, 1);
	put_cursor(wire, cursor(revision));
	m::array(wire, 4);
	m::unsigned_integer(wire, namespaces);
	m::unsigned_integer(wire, order.size());
	m::unsigned_integer(wire, 0);
	m::unsigned_integer(wire, strings);
	m::array(wire, namespaces);
	std::vector<std::string> names;
	for (int ns = 0; ns < namespaces; ++ns)
		names.push_back("n" + std::to_string(ns));
	std::sort(names.begin(), names.end());
	for (const auto &ns : names) {
		m::array(wire, 2);
		m::string(wire, ns);
		m::unsigned_integer(wire, revision);
	}
	m::array(wire, order.size());
	for (const int i : order) {
		m::array(wire, 4);
		m::unsigned_integer(wire, std::find(names.begin(), names.end(), space(i)) - names.begin());
		m::string(wire, name(i));
		m::unsigned_integer(wire, revision);
		m::array(wire, 2);
		m::unsigned_integer(wire, 1);
		m::signed_integer(wire, *expected(revision, i));
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
	return http("Content-Type: application/vnd.detent.distribution\r\nX-Detent-Profile: 1\r\nX-Detent-Revision: " +
					std::to_string(revision) + "\r\nX-Detent-Kind: 1\r\nX-Detent-Coding: raw\r\n" +
					window_headers(window),
				frame);
}
// The delta from revision 1 to 2 on namespace n3.
inline std::string delta_reply(std::uint64_t window) {
	std::map<std::string, std::int64_t> upserts;
	std::vector<std::string> removals;
	delta_changes(upserts, removals);
	std::map<std::string, std::optional<std::int64_t>> changes;
	for (const auto &[key, value] : upserts)
		changes[key] = value;
	for (const auto &key : removals)
		changes[key] = std::nullopt;
	std::vector<std::string> scope;
	for (int ns = 0; ns < namespaces; ++ns)
		scope.push_back("n" + std::to_string(ns));
	std::sort(scope.begin(), scope.end());
	const auto index = std::find(scope.begin(), scope.end(), "n3") - scope.begin();
	std::uint64_t strings = 2;
	for (const auto &[key, value] : changes)
		strings += key.size();
	std::string wire;
	m::array(wire, 7);
	m::unsigned_integer(wire, 1);
	m::unsigned_integer(wire, 2);
	m::array(wire, 2);
	m::unsigned_integer(wire, 1);
	m::unsigned_integer(wire, 1);
	put_cursor(wire, cursor(1));
	put_cursor(wire, cursor(2));
	m::array(wire, 4);
	m::unsigned_integer(wire, 1);
	m::unsigned_integer(wire, changes.size());
	m::unsigned_integer(wire, 0);
	m::unsigned_integer(wire, strings);
	m::array(wire, 1);
	m::array(wire, 3);
	m::unsigned_integer(wire, 2);
	m::array(wire, 1);
	m::array(wire, 2);
	m::string(wire, "n3");
	m::unsigned_integer(wire, 2);
	m::array(wire, changes.size());
	for (const auto &[key, value] : changes) {
		m::array(wire, 4);
		m::unsigned_integer(wire, index);
		m::string(wire, key);
		m::unsigned_integer(wire, value ? 0 : 1);
		if (value) {
			m::array(wire, 2);
			m::unsigned_integer(wire, 1);
			m::signed_integer(wire, *value);
		} else
			wire.push_back('\xc0');
	}
	d::Identity identity{};
	identity.fill('d');
	const auto frame = d::encode_fragment({d::UpdateKind::Delta, identity, 0, wire.size(), true, wire}, false);
	return http("Content-Type: application/vnd.detent.distribution\r\nX-Detent-Profile: 1\r\nX-Detent-Revision: 2\r\n"
				"X-Detent-Kind: 2\r\nX-Detent-Coding: raw\r\n" + window_headers(window),
				frame);
}

// The scripted upstream: a TLS server on its own thread, one request per connection.
struct Server {
	// What a session opened without force offers: nothing, the delta to revision 2 once, or (a session in snapshot
	// mode) the snapshot at the head.
	enum class Resume { Idle, Delta, Snapshot };
	std::atomic<unsigned> head{1};
	std::atomic<Resume> resume{Resume::Idle};
	// A session opened for a Replace also offers the delta after its snapshot, while no delta has been offered.
	std::atomic<bool> forced_delta{false};
	std::atomic<unsigned> opens{0}, forced_opens{0}, snapshots{0}, deltas{0};
	std::atomic<bool> stop{false};
	std::atomic<bool> failed{false};
	asio::io_context io;
	asio::ip::tcp::acceptor acceptor{io, {asio::ip::address_v4::loopback(), 0}};
	std::shared_ptr<asio::ssl::context> server_tls, client_tls;
	std::jthread thread;
	explicit Server(const fs::path &certificates) {
		server_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*server_tls,
								{(certificates / "one.pem").string(), (certificates / "one.key").string(),
								 (certificates / "ca.pem").string()},
								reactor::tls::Role::Server);
		client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*client_tls, {"", "", (certificates / "ca.pem").string()},
								reactor::tls::Role::Client);
		acceptor.non_blocking(true);
		thread = std::jthread([this] { serve(); });
	}
	~Server() {
		stop = true;
		if (thread.joinable())
			thread.join();
	}
	void serve() {
		try {
			while (!stop) {
				asio::ip::tcp::socket accepted(io);
				asio::error_code error;
				acceptor.accept(accepted, error);
				if (error == asio::error::would_block || error == asio::error::try_again) {
					std::this_thread::sleep_for(1ms);
					continue;
				}
				if (error)
					throw asio::system_error(error);
				handle(std::move(accepted));
			}
		} catch (...) {
			failed = true;
		}
	}
	void handle(asio::ip::tcp::socket accepted) {
		asio::ssl::stream<asio::ip::tcp::socket> socket(std::move(accepted), *server_tls);
		asio::error_code error;
		socket.handshake(asio::ssl::stream_base::server, error);
		if (error)
			return;
		asio::streambuf request;
		asio::read_until(socket, request, "\r\n\r\n", error);
		if (error)
			return;
		auto text = [&] { return std::string(asio::buffers_begin(request.data()), asio::buffers_end(request.data())); };
		const auto header = text();
		const auto body_start = header.find("\r\n\r\n") + 4;
		const auto at = header.find("Content-Length: ");
		const std::size_t length = at == std::string::npos ? 0 : std::stoul(header.substr(at + 16));
		if (request.size() < body_start + length)
			asio::read(socket, request, asio::transfer_exactly(body_start + length - request.size()), error);
		const auto body = text().substr(body_start);
		std::string reply;
		if (header.starts_with("POST /v2/distribution/session/open ")) {
			++opens;
			const bool force = body.find("\"force_snapshot\":true") != std::string::npos;
			if (force)
				++forced_opens;
			const bool snapshot = force || resume == Resume::Snapshot || body.find("\"base\":{") == std::string::npos;
			session_snapshot = snapshot;
			resume_offered = false;
			reply = json_reply(std::string("{\"version\":1,\"session\":\"11111111111111111111111111111111\",") +
							   "\"window\":0,\"mode\":\"" + (snapshot ? "snapshot" : "resume") +
							   "\",\"wait_ms\":100}");
			if (force)
				session_forced = true;
			else
				session_forced = false;
		} else if (header.starts_with("POST /v2/distribution/session/next ")) {
			const auto after_at = body.find("\"after\":");
			const std::uint64_t window = std::stoull(body.substr(after_at + 8)) + 1;
			if (session_snapshot) {
				session_snapshot = false;
				++snapshots;
				reply = snapshot_reply(head, window);
			} else if (resume == Resume::Delta && (!session_forced || (forced_delta && deltas == 0)) && !resume_offered) {
				// Every session that was not opened for a Replace is offered the delta once; one that cannot
				// use it asks for the Replace of the head it had, which is then revision 2.
				resume_offered = true;
				++deltas;
				head = 2;
				reply = delta_reply(window);
			} else {
				std::this_thread::sleep_for(20ms);
				reply = "HTTP/1.1 204 No Content\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
			}
		} else {
			reply = "HTTP/1.1 204 No Content\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
		}
		asio::write(socket, asio::buffer(reply), error);
		socket.lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both, error);
	}
	bool session_snapshot = false, session_forced = false, resume_offered = false;
};
inline void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("scripted invariant at line " + std::to_string(location.line()));
}

// The relay bundle behind a count of what the coordinator hands the sink.
class CountingSink final : public t::PublicationSink {
  public:
	explicit CountingSink(std::shared_ptr<detent::relay::Bundle> value) : bundle(std::move(value)) {}
	void authenticated() noexcept override { bundle->authenticated(); }
	void begin_stage(const t::ReceivedUpdate &update) override {
		++stages;
		bundle->begin_stage(update);
	}
	bool stage_step(d::WorkBudget &budget) override { return bundle->stage_step(budget); }
	void bind_candidate(const l::Descriptor &descriptor, l::DirectoryIdentity identity) noexcept override {
		bundle->bind_candidate(descriptor, identity);
	}
	bool release_stage(d::WorkBudget &budget) noexcept override { return bundle->release_stage(budget); }
	void selected(const l::RecoveredGeneration &generation) override {
		++selects;
		bundle->selected(generation);
	}
	void selected_equivalent(const l::RecoveredGeneration &generation, const x::StateEquivalenceProof &proof) override {
		bundle->selected_equivalent(generation, proof);
	}
	void selected_repaired(const l::RecoveredGeneration &generation) override {
		++repairs;
		bundle->selected_repaired(generation);
	}
	void fence() noexcept override { bundle->fence(); }
	std::shared_ptr<detent::relay::Bundle> bundle;
	unsigned stages = 0, selects = 0, repairs = 0;
};

struct Options {
	std::uint64_t scrub_rate = 1'000'000'000;
	bool relay = false;
	bool fresh = false;
	unsigned workers = 1;
	std::optional<d::Cursor> reset_from;
	std::shared_ptr<t::PublicationSink> sink;
};
inline task::task<bool> until(task::asio_scheduler &scheduler, std::chrono::seconds limit, const auto &predicate) {
	asio::steady_timer timer(scheduler.executor());
	const auto deadline = std::chrono::steady_clock::now() + limit;
	while (!predicate()) {
		if (std::chrono::steady_clock::now() >= deadline)
			co_return false;
		timer.expires_after(2ms);
		co_await detent::transport::await_timer(timer);
	}
	co_return true;
}
// Runs one coordinator over `directory` against `server` and hands it to `observe` on its loop; the coordinator is
// closed when `observe` returns, and the phase returns once it has drained.
template <class Observe>
inline void run_phase(const fs::path &certificates, const fs::path &directory, Server &server, Options options,
			   Observe &&observe) {
	auto root = std::make_shared<d::StorageLedger>(768ull * 1024 * 1024);
	auto account = std::make_shared<d::StorageLedger>(root, 512ull * 1024 * 1024);
	d::RetirementRuntime runtime(account, 3);
	auto domain = runtime.domain(account, 2048);
	auto mapping = std::make_shared<d::StorageLedger>(512ull * 1024 * 1024);
	l::DiskStoreOptions disk_options;
	disk_options.mode = options.fresh ? l::StoreOpenMode::FreshEmpty : l::StoreOpenMode::ExistingManaged;
	disk_options.recovery_limits.mapping_account = mapping;
	disk_options.recovery_limits.retirement_domain = &domain;
	disk_options.limits = {512ull * 1024 * 1024, 4096u, 10};
	auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), disk_options);
	asio::io_context io(1);
	task::asio_scheduler scheduler(io.get_executor());
	asio::thread_pool worker(options.workers);
	auto queue = kronuz::io::completion::make_native_queue(
		kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
	auto driver = std::allocate_shared<t::NativeDriver>(d::StorageAllocator<t::NativeDriver>(account), scheduler, queue);
	t::CoordinatorOptions config;
	config.source.scheduler = &scheduler;
	config.directory = directory;
	config.image_format = t::LocalImageFormat::PagedV2;
	config.delivery_mode = t::DeliveryMode::Subscription;
	config.poll_interval = 20ms;
	config.compaction_interval = 0ms;
	config.source.endpoint = server.acceptor.local_endpoint();
	config.source.hostname = "localhost";
	config.source.token = "0123456789abcdef0123456789abcdef";
	config.source.tls_context = server.client_tls;
	config.source.configured = cursor();
	config.reset_from = options.reset_from;
	config.source.compression = false;
	config.source.timeout = 60s;
	for (int ns = 0; ns < namespaces; ++ns)
		config.source.namespaces.push_back("n" + std::to_string(ns));
	std::sort(config.source.namespaces.begin(), config.source.namespaces.end());
	config.view_limits.mapping_account = mapping;
	config.view_limits.retirement_domain = &domain;
	config.scrub_bytes_per_second = options.scrub_rate;
	auto cache_account = std::make_shared<d::StorageLedger>(root, 64ull * 1024 * 1024);
	std::optional<d::RetirementDomain> cache_domain;
	std::shared_ptr<detent::relay::Bundle> bundle;
	std::shared_ptr<CountingSink> counting;
	if (options.relay) {
		cache_domain.emplace(runtime.domain(cache_account, 64 + 500));
		detent::relay::BundleOptions bundle_options;
		bundle_options.configured = cursor();
		bundle_options.namespaces = namespaces;
		bundle_options.incarnation.fill('r');
		bundle = detent::relay::make_bundle(bundle_options, *cache_domain);
		counting = std::make_shared<CountingSink>(bundle);
		config.publication_sink = counting;
	}
	if (options.sink)
		config.publication_sink = options.sink;
	auto coordinator = t::make_coordinator(config, store, driver, domain, worker);
	auto observing = [&]() -> task::task<void> {
		co_await task::when_all(coordinator->run(), [&]() -> task::task<void> {
			try {
				co_await observe(scheduler, coordinator, store, bundle, counting);
			} catch (...) {
				coordinator->close();
				throw;
			}
			coordinator->close();
		}());
	};
	auto done = spawn_future(scheduler, observing());
	io.run();
	done.get();
	worker.join();
	check(!server.failed);
	check(coordinator->drained() && store->drained());
	counting.reset();
	bundle.reset();
	if (cache_domain)
		cache_domain->close().wait();
	domain.close().wait();
}

inline fs::path scratch(const char *what) {
	auto path = fs::current_path() / ".scratch" / (std::string("repair-") + what + "-" + std::to_string(::getpid()));
	fs::remove_all(path);
	fs::create_directories(path);
	return path;
}
inline void copy_store(const fs::path &from, const fs::path &to) {
	fs::remove_all(to);
	fs::copy(from, to, fs::copy_options::recursive);
}
// Publishes revision 1 into a fresh store through the coordinator and returns once it is selected.
inline void seed(const fs::path &certificates, const fs::path &store) {
	fs::create_directories(store);
	fs::permissions(store, fs::perms::owner_all);
	Server server(certificates);
	run_phase(certificates, store, server, {.scrub_rate = 0, .fresh = true},
			  [&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
				  check(co_await until(scheduler, 60s, [&] {
					  const auto status = coordinator->status();
					  return status.acknowledged == cursor() && status.recoverable == cursor();
				  }));
			  });
	check(packs_in(store).size() >= 3);
}
// What the coordinator was doing when a wait ran out.
inline bool explain(bool ok, const std::shared_ptr<t::Coordinator> &coordinator, const Server &server,
			 std::source_location at = std::source_location::current()) {
	if (ok)
		return true;
	const auto status = coordinator->status();
	std::cerr << "line " << at.line() << ": phase " << static_cast<unsigned>(status.phase) << " failures "
			  << status.failures << " repair " << status.repair.requested << "/" << status.repair.completed
			  << (status.repair.pending ? " pending" : "") << " scrub passes " << status.scrub.passes << " damaged "
			  << status.scrub.damaged_packs << " selected "
			  << (status.selected.descriptor ? status.selected.descriptor->cursor.revision : 0) << " opens "
			  << server.opens << " forced " << server.forced_opens << " snapshots " << server.snapshots
			  << " deltas " << server.deltas << '\n';
	try {
		if (status.error)
			std::rethrow_exception(status.error);
	} catch (const std::exception &error) {
		std::cerr << "  error: " << error.what() << '\n';
	}
	return false;
}
inline bool settled(const t::CoordinatorStatus &status) {
	return status.phase != t::CoordinatorPhase::Fenced && status.phase != t::CoordinatorPhase::Closed;
}

// Reads every knob through a reader of the store, as a reader process does.
struct Reads {
	std::uint64_t ok = 0, failed = 0, wrong = 0, other = 0;
	std::vector<char> good;
};
struct Reader {
	std::shared_ptr<d::StorageLedger> account = std::make_shared<d::StorageLedger>(512ull * 1024 * 1024);
	std::shared_ptr<d::StorageLedger> mappings = std::make_shared<d::StorageLedger>(512ull * 1024 * 1024);
	d::RetirementRuntime runtime{account, 1};
	d::RetirementDomain domain = runtime.domain(account, 2048);
	std::optional<x::PreparedGeneration> prepared;
	std::optional<l::RecoveredGeneration> recovered;
	// A reader process (`PreparedReader`) or, with `agent`, the agent's own trusted recovery, which checks each
	// page on first touch and fails only the reads that reach a damaged one.
	explicit Reader(const fs::path &store, bool agent = false) {
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = mappings;
		if (agent) {
			limits.bytes = 1ull << 30;
			x::RecoveryContext context{domain.factory(), account, mappings, limits, {}, {}};
			context.trusted_start = true;
			recovered.emplace(x::RecoveryEngine::recover(store, context));
		} else
			prepared.emplace(x::PreparedReader::open(store, ::geteuid(), limits));
	}
	~Reader() {
		prepared.reset();
		recovered.reset();
		domain.close().wait();
	}
	Reads read(unsigned revision) const {
		Reads reads;
		reads.good.assign(knobs, 0);
		for (int i = 0; i < knobs; ++i) {
			const auto want = expected(revision, i);
			const auto got = recovered ? recovered->view().get(space(i), "k" + std::to_string(i))
									   : prepared->view.get(space(i), "k" + std::to_string(i));
			if (got.has_value()) {
				if (want && got.value() == *want) {
					++reads.ok;
					reads.good[i] = 1;
				} else
					++reads.wrong;
			} else if (got.error().category == detent::ErrorCategory::SourceFailure)
				++reads.failed;
			else if (!want && got.error().category == detent::ErrorCategory::MissingKnob) {
				++reads.ok;
				reads.good[i] = 1;
			} else
				++reads.other;
		}
		return reads;
	}
};
} // namespace scripted_upstream
