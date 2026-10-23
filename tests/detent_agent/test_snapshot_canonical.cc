// Check deterministic JSON and shared-memory bytes across insertion orders.
#include <detent/json_schema.hh>
#include "shm_writer.hh"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template <class Map> void change_buckets(Map& map) {
	if constexpr (requires { map.rehash(4099); })
		map.rehash(4099);
}

detent::Snapshot make_snapshot(unsigned order) {
	detent::Snapshot snapshot;
	snapshot.version = 17;
	snapshot.published_at = "2026-10-22T00:00:00Z";
	std::vector<unsigned> keys(1024);
	std::iota(keys.begin(), keys.end(), 0);
	if (order == 1)
		std::reverse(keys.begin(), keys.end());
	else if (order == 2) {
		std::mt19937 random(7106);
		std::shuffle(keys.begin(), keys.end(), random);
	}
	for (auto key : keys) {
		auto name = "namespace" + std::to_string(key % 7) + ":key" + std::to_string(key);
		if (key % 2) {
			detent::BoolDef definition;
			definition.default_value = key % 3 == 0;
			definition.rollout_pct = 42;
			definition.overrides.emplace("production", detent::GroupRollout{73});
			definition.overrides.emplace("internal", true);
			definition.overrides.emplace("canary", false);
			snapshot.knobs.emplace(std::move(name), std::move(definition));
		} else {
			snapshot.knobs.emplace(std::move(name), detent::IntDef{key});
		}
	}
	if (order == 2)
		change_buckets(snapshot.knobs);
	return snapshot;
}
} // namespace

int main(int argc, char** argv) {
	try {
		if (argc > 2)
			throw std::invalid_argument("at most one output directory allowed");
		const auto first = make_snapshot(0);
		const auto json = detent::serialize_snapshot(first);
		const auto shm = detent_agent::serialize_for_shm(first);
		for (unsigned order : {1U, 2U}) {
			const auto snapshot = make_snapshot(order);
			if (detent::serialize_snapshot(snapshot) != json)
				throw std::runtime_error("insertion order changes JSON bytes");
			const auto candidate = detent_agent::serialize_for_shm(snapshot);
			if (candidate != shm) {
				const auto mismatch = std::mismatch(shm.begin(), shm.end(),
					candidate.begin(), candidate.end());
				throw std::runtime_error("insertion order changes shared-memory byte " +
					std::to_string(mismatch.first - shm.begin()));
			}
		}
		if (argc == 1)
			return 0;
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		auto write = [&](const char* name, const char* bytes, std::size_t size) {
			std::ofstream file(directory / name, std::ios::binary);
			file.exceptions(std::ios::badbit | std::ios::failbit);
			file.write(bytes, static_cast<std::streamsize>(size));
			file.close();
		};
		write("snapshot.json", json.data(), json.size());
		write("snapshot.shm", reinterpret_cast<const char*>(shm.data()), shm.size());
		std::cout << "canonical bytes stable across three insertion schedules\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
