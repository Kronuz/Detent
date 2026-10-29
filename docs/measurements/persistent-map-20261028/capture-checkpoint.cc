#include <fstream>
#include <cstdlib>
// Paired in-memory update benchmark. This does not measure durable ACK
// throughput. Usage: detent_authority_bench [100000|500000] [hot|uniform]
// [overrides=2]
#include "detent/json_patch.hh"
#include "state.hh"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <sys/resource.h>
using namespace detent;
using namespace detent::authority;
using Clock = std::chrono::steady_clock;
namespace {
std::size_t number(const char *arg, std::size_t bound) {
	std::size_t n = 0;
	std::string_view s(arg);
	auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
	if (error != std::errc{} || end != s.data() + s.size() || n > bound || !n)
		throw std::invalid_argument("benchmark argument");
	return n;
}
double ms(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
} // namespace
int main(int argc, char **argv) {
	try {
		auto count = argc > 1 ? number(argv[1], 500000) : 100000;
		bool hot = argc > 2 && std::string_view(argv[2]) == "hot";
		auto overrides = argc > 3 ? number(argv[3], 256) : 2;
		Context context;
		context.cluster[0] = 'c';
		context.group[0] = 'g';
		context.epoch[0] = 'e';
		State state(context);
		Snapshot baseline;
		baseline.version = 1;
		std::vector<std::vector<Mutation>> namespaces(hot ? 1 : 100);
		for (std::size_t i = 0; i < count; ++i) {
			auto ns = i % namespaces.size();
			auto key = "namespace" + std::to_string(ns) + ":service/feature_" + std::to_string(i);
			KnobDef value = IntDef{static_cast<std::int64_t>(i)};
			if (!(i % 2)) {
				BoolDef flag;
				flag.rollout_pct = 25;
				for (std::size_t j = 0; j < overrides; ++j)
					flag.overrides["group" + std::to_string(j)] = GroupRollout{5};
				value = std::move(flag);
			}
			baseline.knobs.emplace(key, value);
			namespaces[ns].push_back({std::move(key), std::move(value), {}, {}});
		}
		Command command;
		command.context = context;
		command.producer = "benchmark";
		command.kind = Kind::Open;
		command.nonce[0] = 'n';
		std::uint64_t index = 1;
		state.apply(index, command);
		command.kind = Kind::Transaction;
		command.nonce = {};
		command.generation = 1;
		auto start = Clock::now();
		auto cpu = std::clock();
		auto batch_size =
			std::min<std::size_t>(128, (state.limits().request_bytes - 1024) / (512 + overrides * 32));
		for (std::size_t ns = 0; ns < namespaces.size(); ++ns) {
			command.name_space = "namespace" + std::to_string(ns);
			auto &rows = namespaces[ns];
			for (std::size_t offset = 0; offset < rows.size(); offset += batch_size) {
				auto n = std::min<std::size_t>(batch_size, rows.size() - offset);
				command.mutations.assign(rows.begin() + offset, rows.begin() + offset + n);
				++command.sequence;
				if (state.apply(++index, command).status != Status::Applied)
					throw std::runtime_error("bootstrap rejected");
			}
		}
		auto load_ms = ms(start);
		auto load_cpu_ms = 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC;
		if (state.size() != baseline.knobs.size())
			throw std::runtime_error("bootstrap count mismatch");
		state.for_each([&](const std::string &key, const Record &record) {
			if (*record.value != baseline.knobs.at(key))
				throw std::runtime_error("bootstrap semantic mismatch");
		});
		namespaces.clear();
		namespaces.shrink_to_fit();
		command.name_space = "namespace0";
		command.mutations = {{"namespace0:service/feature_0", IntDef{-1}, {}, {}}};
		std::vector<double> old_times, new_times;
		auto allocations = state.allocations();
		cpu = std::clock();
		for (int i = 0; i < 21; ++i) {
			SnapshotPatch patch;
			patch.version = 2;
			patch.knobs[command.mutations[0].key] = IntDef{-1 - i};
			start = Clock::now();
			auto result = apply_patch(baseline, patch, count);
			if (!result)
				throw std::runtime_error("baseline patch rejected");
			old_times.push_back(ms(start));
		}
		auto old_cpu_ms = 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC;
		cpu = std::clock();
		for (int i = 0; i < 1000; ++i) {
			command.mutations[0].value = IntDef{-1 - i};
			++command.sequence;
			start = Clock::now();
			auto result = state.apply(++index, command);
			if (result.status != Status::Applied)
				throw std::runtime_error("update rejected");
			new_times.push_back(ms(start));
		}
		auto new_cpu_ms = 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC;
		auto update_allocations = state.allocations() - allocations;
		start = Clock::now();
		ImageEncoder encoder(state);
		auto capture_us = 1000 * ms(start);
        const auto directory = std::getenv("TREE_CAPTURE_DIR");
        if (!directory) throw std::runtime_error("capture directory missing");
        std::ofstream capture(std::string(directory) + "/checkpoint.wire", std::ios::binary);
		std::size_t image_bytes = 0;
		double max_encode_ms = 0;
		start = Clock::now();
		while (!encoder.finished()) {
			auto turn = Clock::now();
			auto chunk = encoder.next();
			max_encode_ms = std::max(max_encode_ms, ms(turn));
			image_bytes += chunk.size();
            capture.write(chunk.data(), chunk.size());
		}
        capture.close();
        if (!capture) throw std::runtime_error("capture write failed");
		auto encode_ms = ms(start);
		std::sort(old_times.begin(), old_times.end());
		std::sort(new_times.begin(), new_times.end());
		rusage usage{};
		getrusage(RUSAGE_SELF, &usage);
#ifndef __APPLE__
		usage.ru_maxrss *= 1024;
#endif
		std::printf("{\"knobs\":%zu,\"namespaces\":%d,\"overrides\":%zu,\"load_ms\":%.3f,"
					"\"load_cpu_ms\":%.3f,\"legacy_patch_median_ms\":%.6f,\"authority_"
					"update_median_ms\":%.6f,\"legacy_cpu_ms_per_update\":%.6f,\"authority_"
					"cpu_ms_per_update\":%.6f,\"nodes_allocated_per_update\":%.2f,"
					"\"retained_nodes\":%zu,\"capture_us\":%.3f,\"image_bytes\":%zu,"
					"\"encode_ms\":%.3f,\"max_encode_turn_ms\":%.3f,\"peak_rss_bytes\":%ld}"
					"\n",
					count, hot ? 1 : 100, overrides, load_ms, load_cpu_ms, old_times[old_times.size() / 2],
					new_times[new_times.size() / 2], old_cpu_ms / old_times.size(),
					new_cpu_ms / new_times.size(), double(update_allocations) / 1000, state.retained_nodes(),
					capture_us, image_bytes, encode_ms, max_encode_ms, usage.ru_maxrss);
	} catch (const std::exception &e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
