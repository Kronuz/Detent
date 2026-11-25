// Tree-cost A/B of the authority's former private map and persistent-map in one binary,
// over the production key shape. Built by hand, not by CMake: the private header comes
// from the baseline commit. One fresh process per sample; prints one JSON line.
//   c++ -std=c++20 -O3 -DNDEBUG -I BASE/src/detent_authority -I PERSISTENT_MAP this.cc
//   cost {private|library} COUNT WIDTH BATCH RETAINED TURNS
#include "persistent_map.hh" // The baseline's private tree: detent::authority::detail.
#include <persistent_map.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::uint64_t cpu() {
	timespec time{};
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time))
		throw std::runtime_error("clock");
	return std::uint64_t(time.tv_sec) * 1000000000 + std::uint64_t(time.tv_nsec);
}
template <class F> std::uint64_t measure(F action) {
	auto start = cpu();
	action();
	return cpu() - start;
}
template <class Map>
int run(const std::vector<std::string> &keys, std::size_t batch, std::size_t retained, std::size_t turns) {
	const auto count = keys.size();
	Map map;
	const auto load = measure([&] {
		for (std::size_t i = 0; i != count; ++i) {
			map = map.set(keys[i], i);
			map.collect(128);
		}
		while (map.reclamation_pending())
			map.collect(128);
	});
	std::uint64_t sum = 0;
	const auto lookup = measure([&] {
		for (std::size_t i = 0; i != 100000; ++i) {
			const auto index = (i * 7919) % count;
			const auto value = map.find(std::string_view(keys[index]));
			if (!value || *value != index)
				throw std::runtime_error("lookup");
			sum += *value;
		}
	});
	std::vector<Map> versions;
	versions.reserve(retained + 1);
	const auto allocations = map.allocations();
	const auto update = measure([&] {
		for (std::size_t step = 0; step != turns; ++step) {
			versions.push_back(map);
			if (versions.size() > retained)
				versions.erase(versions.begin());
			for (std::size_t i = 0; i != batch; ++i)
				map = map.set(keys[(step * batch + i) % count], count + step);
			map.collect(128);
		}
	});
	const auto nodes = map.allocations() - allocations;
	const auto retained_nodes = map.retained_nodes();
	std::size_t turns_collected = 0;
	const auto collect = measure([&] {
		versions.clear();
		map.retire_root();
		while (map.reclamation_pending()) {
			map.collect(128);
			++turns_collected;
		}
	});
	std::printf(",\"load_ns\":%llu,\"lookup_ns\":%llu,\"update_ns\":%llu,\"collect_ns\":%llu,"
				"\"allocated_nodes\":%zu,\"retained_nodes\":%zu,\"collection_turns\":%zu,\"sum\":%llu}\n",
				(unsigned long long)load, (unsigned long long)lookup, (unsigned long long)update,
				(unsigned long long)collect, nodes, retained_nodes, turns_collected, (unsigned long long)sum);
	return 0;
}
} // namespace
int main(int argc, char **argv) {
	try {
		if (argc != 7)
			throw std::invalid_argument("arm count width batch retained turns");
		const std::string_view arm = argv[1];
		const auto count = std::stoull(argv[2]), width = std::stoull(argv[3]);
		const auto batch = std::stoull(argv[4]), retained = std::stoull(argv[5]), turns = std::stoull(argv[6]);
		if (!count || width < 34 || width > 4096 || !batch || !retained || !turns)
			throw std::invalid_argument("bounds");
		std::vector<std::string> keys;
		keys.reserve(count);
		for (std::size_t i = 0; i != count; ++i) {
			auto key = "namespace0:service/feature_" + std::to_string(i);
			key.append(width - key.size(), 'x');
			keys.push_back(std::move(key));
		}
		std::printf("{\"arm\":\"%s\",\"count\":%llu,\"width\":%llu,\"batch\":%llu,\"retained\":%llu,\"turns\":%llu",
					argv[1], count, width, batch, retained, turns);
		if (arm == "private")
			return run<detent::authority::detail::PersistentMap<std::string, std::uint64_t>>(keys, batch, retained,
																							  turns);
		if (arm == "library")
			return run<persistent_map::PersistentMap<std::string, std::uint64_t>>(keys, batch, retained, turns);
		throw std::invalid_argument("arm");
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
