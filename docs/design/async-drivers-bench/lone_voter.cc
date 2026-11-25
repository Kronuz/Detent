// Review X5: how long a lone voter takes to lead after Start, on consensus as pinned. A group
// whose only voter is this node is created on the shared turn log, started at 2 ms, and ticked
// every millisecond with Timing's default election delay; the first tick at which it leads is
// printed. D5 and Xapiand's reopen-on-demand need it to lead at Start (core.h:430-433 resets the
// election deadline there; :1001-1004 campaign only once it passes): exits 1 while it waits.
#include "completion/posix.h"
#include "consensus/worker.h"
#include "journal/store.h"
#include "journal/turn_driver.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace j = kronuz::journal;
namespace c = kronuz::consensus;

namespace {
const j::AdmissionLimits admission{{1024ull * 1024 * 1024, 65536}, {8ull * 1024 * 1024, 3}, {512ull * 1024 * 1024, 4}, 64, 3};
const j::TurnLogOptions turn_options{std::uint64_t{1} << 20, 32 << 10, 1, 4, 1 << 20};

fs::path directory(const fs::path& root, const std::string& name) {
	auto path = root / name;
	fs::create_directories(path);
	fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
	return path;
}
j::Identity identity(char tag) {
	j::Identity id{};
	id[0] = tag;
	return id;
}
void pump(c::Worker& worker, j::TurnDriver& turns, c::Tick tick) {  // as idle_cost.cc
	for (int i = 0; i < 64; ++i) {
		auto result = worker.run_one(tick);
		while (auto operation = worker.take_io_operation()) {
			j::drive_synchronously(*operation);
			result = worker.run_one(tick);
		}
		for (const auto& action : worker.take_actions()) {
			if (auto committed = std::get_if<c::Committed>(&action)) { worker.applied({committed->entries.back().index}); }
		}
		while (!turns.idle()) { turns.commit(); }
		if (result == c::TurnResult::Fenced) { throw std::runtime_error("worker fenced"); }
	}
}
}  // namespace

int main() {
	const fs::path root = fs::path(std::getenv("BENCH_DIR") ? std::getenv("BENCH_DIR") : "bench-data") / "lone-voter";
	fs::remove_all(root);
	fs::create_directories(root);
	long first = -1;
	const c::Timing timing{};
	const std::uint64_t delay = 1500;
	{
		auto log_io = std::make_shared<j::PosixIO>(directory(root, "log"));
		j::TurnDriver turns(*log_io, identity('L'), turn_options);
		auto io = std::make_shared<j::PosixIO>(directory(root, "g1"));
		c::FixedConfiguration config;
		config.local = 1;
		config.voters = {1};
		config.cluster[0] = 'B';
		config.configuration[15] = 1;
		c::WorkerLimits scheduling;
		const c::Limits defaults;
		scheduling.retained = {defaults.command_bytes, defaults.log_bytes, defaults.log_entries, defaults.rpc_bytes, defaults.rpc_entries};
		c::Worker worker(io, turns, 1, config, admission, c::Limits{}, timing, scheduling, c::SnapshotLimits{0x44544101, 64ull << 20});
		worker.create(identity('C'));
		pump(worker, turns, {1, delay});
		if (!worker.ready()) { throw std::runtime_error("group not ready"); }
		if (worker.try_submit(c::Start{}) != c::SubmitResult::Accepted) { throw std::runtime_error("start refused"); }
		for (std::uint64_t now = 2; now <= 4000; ++now) {
			pump(worker, turns, {now, delay});
			if (worker.role() == c::Role::Leader) {
				first = static_cast<long>(now);
				break;
			}
		}
	}
	fs::remove_all(root);
	const bool ok = first == 2;
	std::printf("lone_voter started_ms=2 first_leader_ms=%ld election_delay_ms=%llu want=2 (leads at Start) %s\n", first,
		static_cast<unsigned long long>(delay), ok ? "ok" : "FAILS");
	return ok ? 0 : 1;
}
