// Memory retirement only: 500k live keys and a 64 MiB retained log.
// Fresh-process comparison: default posted retirement, or --synchronous.
#include "state.hh"
#include <asio.hpp>
#include <consensus/core.h>
#include <cstdio>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <sys/resource.h>
#include <time.h>
using namespace detent::authority;
static void require(bool value) {
	if (!value)
		throw std::runtime_error("terminal benchmark invariant");
}
static std::uint64_t cpu() {
	timespec t{};
	require(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0);
	return t.tv_sec * 1000000000ull + t.tv_nsec;
}
int main(int argc, char **argv) {
	const bool synchronous = argc == 2 && std::string_view(argv[1]) == "--synchronous";
	Context context;
	context.cluster[0] = 'c';
	context.group[0] = 'g';
	context.epoch[0] = 'e';
	auto state = std::make_unique<State>(context);
	Command command;
	command.context = context;
	command.kind = Kind::Open;
	command.producer = "writer";
	command.nonce[0] = 'n';
	require(state->apply(1, command).status == Status::Applied);
	command.kind = Kind::Transaction;
	command.nonce = {};
	command.generation = 1;
	command.name_space = "ns";
	std::uint64_t index = 1;
	for (std::size_t first = 0; first < 500000; first += 128) {
		command.sequence++;
		command.mutations.clear();
		for (std::size_t i = first; i < std::min(first + 128, std::size_t{500000}); ++i)
			command.mutations.push_back(
				{"ns:k" + std::to_string(i), detent::IntDef{static_cast<std::int64_t>(i)}, {}, {}});
		require(state->apply(++index, command).status == Status::Applied);
		while (state->reclamation_pending())
			state->collect(8192);
	}
	require(state->size() == 500000);
	kronuz::consensus::FixedConfiguration config;
	config.local = 1;
	config.voters = {1};
	config.cluster[0] = 'c';
	config.configuration[0] = 'v';
	kronuz::consensus::RecoveredState recovered;
	recovered.configuration = config;
	recovered.hard = {1, 1, 0};
	for (std::uint64_t i = 1; i <= 65536; ++i)
		recovered.entries.push_back({i, 1, kronuz::consensus::EntryKind::Command, std::string(1024, 'x')});
	auto core = std::make_unique<kronuz::consensus::Core>(config, std::move(recovered));
	std::uint64_t max = 0, total = 0, turns = 0, heartbeats = 0, reset = 0;
	std::uint64_t peak_state = 0, peak_log = 0, peak_tail = 0, peak_wall = 0, peak_turn = 0;
	if (synchronous) {
		auto begin = cpu();
		state.reset();
		core.reset();
		reset = cpu() - begin;
		max = reset;
		total = reset;
	} else {
		asio::io_context owner;
		bool roots_retired = false;
		std::function<void()> retire;
		retire = [&] {
			const auto wall_started = std::chrono::steady_clock::now();
			auto begin = cpu();
			if (!roots_retired) {
				state->retire();
				core->begin_retirement();
				roots_retired = true;
			}
			CommandWorkBudget budget;
			state->collect(budget);
			const auto state_finished = cpu();
			kronuz::consensus::detail::RetirementBudget log_budget{32, 1024 * 1024};
			core->retirement_step(log_budget);
			const auto log_finished = cpu();
			const auto turn_started = begin;
			++turns;
			asio::post(owner, [&] { ++heartbeats; });
			if (state->reclamation_pending() || !core->retirement_drained()) {
				asio::post(owner, retire);
			} else {
				require(state->retained_nodes() == 0 && state->retired_values() == 0 &&
						core->retired_log_entries() == 0);
				begin = cpu();
				state.reset();
				core.reset();
				reset = cpu() - begin;
			}
			const auto finished = cpu();
			auto elapsed = finished - turn_started;
			if (elapsed > max) {
				max = elapsed;
				peak_state = state_finished - turn_started;
				peak_log = log_finished - state_finished;
				peak_tail = finished - log_finished;
				peak_turn = turns;
				peak_wall = std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - wall_started).count();
			}
			total += elapsed;
		};
		asio::post(owner, retire);
		owner.run();
		require(heartbeats == turns && turns > 30000);
	}
	std::printf("{\"diagnostic_peak_turn\":%llu,\"state_cpu_ns\":%llu,\"log_cpu_ns\":%llu,\"tail_cpu_ns\":%llu,\"wall_at_cpu_peak_ns\":%llu}\n",
		(unsigned long long)peak_turn,(unsigned long long)peak_state,(unsigned long long)peak_log,
		(unsigned long long)peak_tail,(unsigned long long)peak_wall);
	rusage usage{};
	require(getrusage(RUSAGE_SELF, &usage) == 0);
	std::printf("{\"mode\":\"%s\",\"interleaved_posts\":%llu,\"live_records\":500000,\"log_entries\":65536,"
				"\"log_payload_bytes\":67108864,\"retirement_turns\":%llu,\"max_turn_cpu_ns\":%llu,\"total_"
				"retirement_cpu_ns\":%llu,\"final_reset_cpu_ns\":%llu,\"peak_rss_native\":%ld}\n",
				synchronous ? "synchronous" : "resumable", (unsigned long long)heartbeats,
				(unsigned long long)turns, (unsigned long long)max, (unsigned long long)total,
				(unsigned long long)reset, usage.ru_maxrss);
}
