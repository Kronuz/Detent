// What an idle group costs today, per library: live heap bytes, descriptors and threads for
// N groups on one core (N = 1 and 1,000), divided by N. A group is idle once it is ready,
// started, and has nothing pending. The last row is Detent's per-authority runtime today
// (service.cc:1147-1161, owner_loop.hh:26): its loop, thread, timer and completion queue.
#include "probe.hh"
PROBE_ALLOCATOR

#include "completion/asio_completion.h"
#include "completion/native_completion.h"
#include "consensus/snapshot_channel.h"
#include "consensus/worker.h"
#include "journal/store.h"
#include "journal/turn_driver.h"

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace j = kronuz::journal;
namespace c = kronuz::consensus;

namespace {
fs::path root;
// Detent's authority storage limits (node.hh:24-25).
const j::AdmissionLimits admission{{1024ull * 1024 * 1024, 65536}, {8ull * 1024 * 1024, 3}, {512ull * 1024 * 1024, 4}, 64, 3};
const j::TurnLogOptions turn_options{std::uint64_t{1} << 20, 32 << 10, 1, 4, 1 << 20};

fs::path directory(const std::string& name) {
	auto path = root / name;
	fs::create_directories(path);
	fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
	return path;
}
j::Identity identity(char tag, std::uint64_t n) {
	j::Identity id{}; id[0] = tag;
	for (int i = 0; i < 8; ++i) { id[1 + i] = static_cast<char>((n >> (8 * i)) & 0xff); }
	return id;
}
c::FixedConfiguration configuration(std::uint64_t group, std::vector<c::NodeId> voters) {
	c::FixedConfiguration config; config.local = 1; config.voters = std::move(voters);
	config.cluster[0] = 'B';
	for (int i = 0; i < 8; ++i) { config.configuration[i] = static_cast<char>((group >> (8 * i)) & 0xff); }
	config.configuration[15] = 1;
	return config;
}
c::WorkerLimits detent_scheduling() {  // node.hh:18-23
	c::WorkerLimits result; const c::Limits defaults;
	result.retained = {defaults.command_bytes, defaults.log_bytes, defaults.log_entries, defaults.rpc_bytes, defaults.rpc_entries};
	return result;
}

// Pumps a Worker the way consensus's own tests do (shared_worker_test.cc:121-142), executing
// IO synchronously, until it reports nothing to do.
void pump(c::Worker& worker, j::TurnDriver* turns, c::Tick tick) {
	for (int i = 0; i < 64; ++i) {
		auto result = worker.run_one(tick);
		while (auto operation = worker.take_io_operation()) { j::drive_synchronously(*operation); result = worker.run_one(tick); }
		for (const auto& action : worker.take_actions()) {
			if (auto committed = std::get_if<c::Committed>(&action)) { worker.applied({committed->entries.back().index}); }
		}
		if (turns) { while (!turns->idle()) { turns->commit(); } }
		if (result == c::TurnResult::Fenced) { throw std::runtime_error("worker fenced"); }
	}
}

struct Row {
	std::string name;
	std::size_t object_bytes;  // sizeof the per-group objects
	std::function<std::function<void()>(std::size_t groups)> build;  // returns the teardown
};

void report(const std::string& name, std::size_t object_bytes, std::size_t groups, const probe::Sample& before, const probe::Sample& after) {
	const double n = static_cast<double>(groups);
	std::printf("row=%s groups=%zu sizeof=%zu heap_per_group=%.0f blocks_per_group=%.1f fds_per_group=%.3f threads_per_group=%.3f heap_total=%lld fds_total=%d threads_total=%d\n",
		name.c_str(), groups, object_bytes, (after.heap - before.heap) / n, (after.blocks - before.blocks) / n,
		(after.fds - before.fds) / n, (after.threads - before.threads) / n,
		static_cast<long long>(after.heap - before.heap), after.fds - before.fds, after.threads - before.threads);
	std::fflush(stdout);
}

template <class Make>
void measure(const std::string& name, std::size_t object_bytes, std::size_t groups, Make make) {
	static int run = 0;
	root = fs::path(std::getenv("BENCH_DIR") ? std::getenv("BENCH_DIR") : "bench-data") / (name + "-" + std::to_string(groups) + "-" + std::to_string(++run));
	fs::remove_all(root);
	fs::create_directories(root);
	auto before = probe::sample();
	auto teardown = make(groups);
	auto after = probe::sample();
	report(name, object_bytes, groups, before, after);
	teardown();
	fs::remove_all(root);
}

// A journal group on the core's shared turn log (D11): a Store opened on the TurnDriver.
std::function<void()> journal_shared(std::size_t groups) {
	auto log_io = std::make_shared<j::PosixIO>(directory("log"));
	auto turns = std::make_shared<j::TurnDriver>(*log_io, identity('L', 0), turn_options);
	auto before = probe::sample();
	auto stores = std::make_shared<std::vector<std::unique_ptr<j::Store>>>();
	for (std::size_t g = 1; g <= groups; ++g) {
		auto io = std::make_shared<j::PosixIO>(directory("g" + std::to_string(g)));
		auto& store = stores->emplace_back(std::make_unique<j::Store>(io, *turns, g, admission));
		store->create(identity('J', g));
		while (!store->accounting()) { store->inventory_step(128); }
		while (!turns->idle()) { turns->commit(); }
	}
	report("journal-shared-store", sizeof(j::Store), groups, before, probe::sample());
	return [stores, turns, log_io] { stores->clear(); };
}

// A journal group with its own journal (the layout Detent's authority uses today).
std::function<void()> journal_own(std::size_t groups) {
	auto stores = std::make_shared<std::vector<std::unique_ptr<j::Store>>>();
	for (std::size_t g = 1; g <= groups; ++g) {
		auto io = std::make_shared<j::PosixIO>(directory("g" + std::to_string(g)));
		auto& store = stores->emplace_back(std::make_unique<j::Store>(io, admission));
		store->create(identity('O', g));
		while (!store->accounting()) { store->inventory_step(128); }
	}
	return [stores] { stores->clear(); };
}

struct Group {
	std::unique_ptr<c::Worker> worker;
	std::unique_ptr<c::SnapshotChannel> channel;
};

// A consensus group on the shared turn log: a Worker on the TurnDriver, started and idle.
// voters = {1}: a single-voter leader; {1, 2, 3}: a follower that has heard from nobody.
std::function<void()> consensus_shared(std::size_t groups, std::vector<c::NodeId> voters, bool channel, const char* name) {
	auto log_io = std::make_shared<j::PosixIO>(directory("log"));
	auto turns = std::make_shared<j::TurnDriver>(*log_io, identity('L', 0), turn_options);
	auto before = probe::sample();
	auto all = std::make_shared<std::vector<Group>>();
	all->reserve(groups);
	const c::Timing timing{};
	for (std::size_t g = 1; g <= groups; ++g) {
		auto io = std::make_shared<j::PosixIO>(directory("g" + std::to_string(g)));
		auto& group = all->emplace_back();
		group.worker = std::make_unique<c::Worker>(io, *turns, g, configuration(g, voters), admission, c::Limits{}, timing, detent_scheduling(), c::SnapshotLimits{0x44544101, 64ull << 20});
		group.worker->create(identity('C', g));
		if (channel) { group.channel = std::make_unique<c::SnapshotChannel>(*group.worker); }
		pump(*group.worker, turns.get(), {1, 1500});
		if (!group.worker->ready()) { throw std::runtime_error("group not ready"); }
		if (group.worker->try_submit(c::Start{}) != c::SubmitResult::Accepted) { throw std::runtime_error("start refused"); }
		pump(*group.worker, turns.get(), {2, 1500});
		if (voters.size() == 1) {
			pump(*group.worker, turns.get(), {2000, 1500});
			if (group.worker->role() != c::Role::Leader) { throw std::runtime_error("single voter did not lead"); }
		}
		if (group.channel) { group.channel->run_one({2000, 1500}); }
	}
	report(name, sizeof(c::Worker) + (channel ? sizeof(c::SnapshotChannel) : 0), groups, before, probe::sample());
	return [all, turns, log_io] { all->clear(); };
}

// Detent's authority group today: its own journal, async storage (service.cc:1462), a
// SnapshotChannel (node.cc:82).
std::function<void()> consensus_own(std::size_t groups) {
	auto all = std::make_shared<std::vector<Group>>();
	all->reserve(groups);
	auto scheduling = detent_scheduling(); scheduling.async_storage = true;
	for (std::size_t g = 1; g <= groups; ++g) {
		auto io = std::make_shared<j::PosixIO>(directory("g" + std::to_string(g)));
		auto& group = all->emplace_back();
		group.worker = std::make_unique<c::Worker>(std::shared_ptr<j::IO>(io), configuration(g, {1}), admission, c::Limits{}, c::Timing{}, scheduling, c::SnapshotLimits{0x44544101, 64ull << 20});
		group.worker->create(identity('D', g));
		group.channel = std::make_unique<c::SnapshotChannel>(*group.worker);
		pump(*group.worker, nullptr, {1, 1500});
		if (group.worker->try_submit(c::Start{}) != c::SubmitResult::Accepted) { throw std::runtime_error("start refused"); }
		pump(*group.worker, nullptr, {2000, 1500});
		if (group.worker->role() != c::Role::Leader) { throw std::runtime_error("single voter did not lead"); }
	}
	return [all] { all->clear(); };
}

// Detent's per-authority runtime today: an OwnerLoop (io_context, scheduler, work guard and
// a thread running it, owner_loop.hh:26), the 1 ms loop timer (service.cc:2603-2605) armed,
// and a native completion queue with its driver (service.cc:1150-1160).
struct Runtime {
	asio::io_context io;
	task::asio_scheduler scheduler{io.get_executor()};
	asio::executor_work_guard<asio::io_context::executor_type> guard = asio::make_work_guard(io);
	asio::steady_timer timer{io};
	std::shared_ptr<j::NativeCompletionQueue> queue = std::make_shared<j::NativeCompletionQueue>();
	std::shared_ptr<j::AsioCompletionDriver<j::NativeCompletionQueue>> driver =
		std::make_shared<j::AsioCompletionDriver<j::NativeCompletionQueue>>(scheduler, queue, true);
	std::thread thread;
	Runtime() {
		timer.expires_after(std::chrono::hours(1));
		timer.async_wait([](const asio::error_code&) {});
		thread = std::thread([this] { io.run(); });
	}
	~Runtime() { guard.reset(); io.stop(); thread.join(); }
};
std::function<void()> runtime_today(std::size_t groups) {
	auto all = std::make_shared<std::vector<std::unique_ptr<Runtime>>>();
	for (std::size_t g = 0; g < groups; ++g) { all->push_back(std::make_unique<Runtime>()); }
	std::this_thread::sleep_for(std::chrono::milliseconds(50));  // every loop thread is up
	return [all] { all->clear(); };
}
}  // namespace

int main(int argc, char** argv) {
	probe::raise_descriptor_limit();
	std::vector<std::size_t> counts{1, 1000};
	if (argc > 1) { counts = {static_cast<std::size_t>(std::stoull(argv[1]))}; }
	std::printf("host_threads_baseline=%d fds_baseline=%d\n", probe::threads(), probe::descriptors());
	for (auto n : counts) {
		measure("journal-shared", sizeof(j::Store), n, journal_shared);
		measure("journal-own", sizeof(j::Store), n, journal_own);
		measure("consensus-shared-leader", sizeof(c::Worker), n, [](std::size_t g) { return consensus_shared(g, {1}, false, "consensus-shared-leader-only"); });
		measure("consensus-shared-follower3", sizeof(c::Worker) + sizeof(c::SnapshotChannel), n, [](std::size_t g) { return consensus_shared(g, {1, 2, 3}, true, "consensus-shared-follower3-only"); });
		measure("consensus-shared-follower3-nochannel", sizeof(c::Worker), n, [](std::size_t g) { return consensus_shared(g, {1, 2, 3}, false, "consensus-shared-follower3-nochannel-only"); });
		measure("consensus-own-detent", sizeof(c::Worker) + sizeof(c::SnapshotChannel), n, consensus_own);
	}
	for (std::size_t n : {std::size_t{1}, std::size_t{8}}) { measure("runtime-today", sizeof(Runtime), n, runtime_today); }
	return 0;
}
