// Real single-voter POSIX durability and 64-producer grouping benchmark.
// Usage: detent_authority_durable_bench NEW_DIRECTORY [knobs=100000] [producers=64]
#include "node.hh"
#include <charconv>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <completion/posix.h>
#include <sys/resource.h>
#include <sys/stat.h>
using namespace detent::authority;
using namespace kronuz::consensus;
using Clock = std::chrono::steady_clock;
namespace {
void check(bool valid, const char *message) {
	if (!valid)
		throw std::runtime_error(message);
}
double ms(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::uint64_t disk_bytes(const std::filesystem::path &directory) {
	std::uint64_t bytes = 0;
	for (const auto &file : std::filesystem::recursive_directory_iterator(directory))
		if (file.is_regular_file())
			bytes += file.file_size();
	return bytes;
}
} // namespace
int main(int argc, char **argv) {
	try {
		check(argc >= 2 && argc <= 5,
			  "usage: detent_authority_durable_bench NEW_DIRECTORY [knobs] [producers] [heavy]");
		std::size_t count = 100000;
		if (argc >= 3) {
			std::string_view arg(argv[2]);
			auto [end, error] = std::from_chars(arg.data(), arg.data() + arg.size(), count);
			check(error == std::errc{} && end == arg.data() + arg.size() && count >= 64 && count <= 500000,
				  "invalid count");
		}
		std::size_t producers = 64;
		if (argc >= 4) {
			std::string_view arg(argv[3]);
			auto [end, error] = std::from_chars(arg.data(), arg.data() + arg.size(), producers);
			check(error == std::errc{} && end == arg.data() + arg.size() && producers > 0 && producers <= 64,
				  "invalid producers");
		}
		const bool heavy = argc == 5;
		check(!heavy || std::string_view(argv[4]) == "heavy", "invalid definition workload");
		std::filesystem::path directory(argv[1]);
		check(std::filesystem::create_directory(directory), "benchmark directory must be new");
		check(chmod(directory.c_str(), 0700) == 0, "directory permissions");
		NodeOptions options;
		options.context.cluster[0] = 'c';
		options.context.group[0] = 'g';
		options.context.epoch[0] = 'e';
		options.configuration.cluster = options.context.cluster;
		options.configuration.configuration = options.context.group;
		options.configuration.local = 1;
		options.configuration.voters = {1};
		Node node(std::make_unique<kronuz::journal::PosixIO>(directory), options);
		Identity storage{};
		storage[0] = 'b';
		node.create(storage);
		std::uint64_t tick = 0, request = 0, logical_bytes = 0;
		double worst_turn_ms = 0, worst_turn_cpu_ms = 0;
		auto pump = [&] {
			auto start = Clock::now();
			auto turn_cpu = std::clock();
			node.service({tick += 10, options.timing.election_min});
			for (const auto &action : node.take_network_actions())
				check(!std::holds_alternative<Send>(action), "unexpected peer traffic");
			check(!node.worker().fenced(), "worker fenced");
			worst_turn_ms = std::max(worst_turn_ms, ms(start));
			worst_turn_cpu_ms =
				std::max(worst_turn_cpu_ms, 1000.0 * (std::clock() - turn_cpu) / CLOCKS_PER_SEC);
		};
		for (unsigned i = 0; i < 10000 && !node.worker().ready(); ++i)
			pump();
		check(node.worker().ready(), "initialization timeout");
		node.start();
		for (unsigned i = 0; i < 10000 && node.worker().role() != Role::Leader; ++i)
			pump();
		check(node.worker().role() == Role::Leader, "election timeout");
		auto submit = [&](const std::vector<Command> &commands) {
			logical_bytes +=
				encode_batch(commands, options.application, options.consensus.command_bytes).size();
			auto id = ++request;
			bool accepted = false;
			for (unsigned i = 0; i < 100000; ++i) {
				if (!accepted) {
					auto result = node.propose(id, commands);
					check(result == SubmitResult::Accepted || result == SubmitResult::Busy ||
							  result == SubmitResult::Pressure,
						  "proposal rejected");
					accepted = result == SubmitResult::Accepted;
				}
				pump();
				for (auto &result : node.take_completions())
					if (result.request == RequestId{id}) {
						check(!result.rejection && result.results.size() == commands.size(),
							  "completion rejected");
						for (auto outcome : result.results)
							check(outcome.status == Status::Applied, "transaction rejected");
						return;
					}
			}
			throw std::runtime_error("durable completion timeout");
		};
		std::vector<Command> commands(64);
		for (std::size_t i = 0; i < commands.size(); ++i) {
			auto &c = commands[i];
			c.context = options.context;
			c.producer = "publisher" + std::to_string(i);
			c.kind = Kind::Open;
			c.nonce[0] = static_cast<char>(i + 1);
		}
		submit(commands);
		for (auto &c : commands) {
			c.kind = Kind::Transaction;
			c.nonce = {};
			c.generation = 1;
			c.name_space = "ns";
		}
		std::optional<detent::KnobDef> heavy_definition;
		std::size_t heavy_command_bytes = 0, heavy_override_count = 0;
		if (heavy) {
			detent::BoolDef definition;
			auto probe = commands.front();
			probe.sequence = 1;
			for (std::size_t overrides = 0; overrides <= options.application.overrides; ++overrides) {
				probe.mutations.clear();
				for (unsigned key = 0; key < 128; ++key)
					probe.mutations.push_back({"ns:feature_" + std::to_string(key), definition, {}, {}});
				try {
					heavy_command_bytes = encode(probe, options.application).size();
				} catch (const std::invalid_argument &) {
					break;
				}
				heavy_definition = definition;
				heavy_override_count = definition.overrides.size();
				definition.overrides["group" + std::to_string(overrides) + std::string(40, 'x')] =
					detent::GroupRollout{25};
			}
			check(heavy_definition.has_value(), "heavy definition cannot fit the profile");
		}
		auto start = Clock::now();
		auto cpu = std::clock();
		std::size_t key = 0;
		while (key < count) {
			std::vector<Command> batch;
			for (auto &c : commands) {
				c.mutations.clear();
				for (unsigned i = 0; i < 128 && key < count; ++i, ++key) {
					detent::KnobDef value = detent::IntDef{static_cast<std::int64_t>(key)};
					if (!(key % 2)) {
						detent::BoolDef flag;
						flag.rollout_pct = 25;
						flag.overrides["group0"] = detent::GroupRollout{5};
						flag.overrides["group1"] = detent::GroupRollout{5};
						value = std::move(flag);
					}
					if (heavy_definition)
						value = *heavy_definition;
					c.mutations.push_back({"ns:feature_" + std::to_string(key), std::move(value), {}, {}});
				}
				if (c.mutations.empty())
					break;
				++c.sequence;
				batch.push_back(c);
			}
			submit(batch);
		}
		auto load_ms = ms(start);
		auto load_turn_cpu_ms = worst_turn_cpu_ms;
		auto load_turn_ms = worst_turn_ms;
		auto load_cpu_ms = 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC;
		commands.resize(producers);
		auto before = disk_bytes(directory), before_logical = logical_bytes;
		worst_turn_ms = worst_turn_cpu_ms = 0;
		start = Clock::now();
		cpu = std::clock();
		for (int group = 0; group < 100; ++group) {
			for (std::size_t i = 0; i < commands.size(); ++i) {
				auto &c = commands[i];
				c.mutations = {{"ns:feature_" + std::to_string(i), detent::IntDef{-1000 - group}, {}, {}}};
				++c.sequence;
			}
			submit(commands);
		}
		auto ack_ms = ms(start);
		auto ack_cpu_ms = 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC;
		auto after = disk_bytes(directory);
		auto update_turn = worst_turn_ms;
		for (unsigned i = 0; i < 10000 && node.worker().applied_index() != node.state().applied_index(); ++i)
			pump();
		start = Clock::now();
		bool started = false;
		double capture_us = 0;
		for (unsigned i = 0; i < 10000 && !started; ++i) {
			auto attempt = Clock::now();
			started = node.checkpoint();
			capture_us = 1000 * ms(attempt);
			if (!started)
				pump();
		}
		check(started, "checkpoint admission timeout");
		auto admission_ms = ms(start);
		auto boundary = node.state().applied_index();
		worst_turn_ms = worst_turn_cpu_ms = 0;
		start = Clock::now();
		for (unsigned i = 0; i < 100000 && (node.worker().base_index() != boundary || node.checkpoint_busy());
			 ++i)
			pump();
		check(node.worker().base_index() == boundary && !node.checkpoint_busy(),
			  "checkpoint completion timeout");
		auto checkpoint_ms = ms(start);
		auto final_bytes = disk_bytes(directory);
		for (unsigned i = 0; i < 1024; ++i)
			pump();
		auto settled_bytes = disk_bytes(directory);
		ImageEncoder image(node.state());
		std::size_t image_bytes = 0;
		while (!image.finished())
			image_bytes += image.next().size();
		rusage usage{};
		getrusage(RUSAGE_SELF, &usage);
#ifndef __APPLE__
		usage.ru_maxrss *= 1024;
#endif
		std::printf("{\"heavy_command_bytes\":%zu,\"heavy_overrides_per_definition\":%zu,\"knobs\":%zu,"
					"\"producers\":%zu,\"grouped_transactions\":%zu,\"load_ms\":%.3f,\"load_"
					"cpu_ms\":%.3f,\"max_load_service_turn_ms\":%.3f,\"max_load_service_turn_cpu_ms\":%.3f,"
					"\"durable_ack_ms_per_transaction\":%.6f,\"durable_cpu_ms_per_"
					"transaction\":%.6f,\"max_update_service_turn_ms\":%.3f,\"checkpoint_capture_us\":%.3f,"
					"\"checkpoint_admission_ms\":%.3f,"
					"\"checkpoint_ms\":%.3f,\"max_checkpoint_service_turn_ms\":%.3f,\"bytes_before_updates\":"
					"%llu,\"bytes_after_updates\":%llu,\"encoded_update_bytes\":%llu,\"bytes_after_"
					"checkpoint\":%llu,\"bytes_after_maintenance\":%llu,\"encoded_total_command_bytes\":%llu,"
					"\"application_image_bytes\":%zu,\"peak_rss_bytes\":%ld}\n",
					heavy_command_bytes, heavy_override_count, count, producers, producers * 100, load_ms,
					load_cpu_ms, load_turn_ms, load_turn_cpu_ms, ack_ms / (producers * 100),
					ack_cpu_ms / (producers * 100), update_turn, capture_us, admission_ms, checkpoint_ms,
					worst_turn_ms, (unsigned long long)before, (unsigned long long)after,
					(unsigned long long)(logical_bytes - before_logical), (unsigned long long)final_bytes,
					(unsigned long long)settled_bytes, (unsigned long long)logical_bytes, image_bytes,
					usage.ru_maxrss);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
