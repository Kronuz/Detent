#include "persistent_map.hh"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <malloc/malloc.h>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

using Map = detent::authority::detail::PersistentMap<std::string, std::uint64_t>;
using Clock = std::chrono::steady_clock;
static std::uint64_t cpu() {
    timespec time{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time)) throw std::runtime_error("clock");
    return std::uint64_t(time.tv_sec) * 1000000000 + time.tv_nsec;
}
template <class F> static std::uint64_t measure(F action) {
    auto start = cpu();
    action();
    return cpu() - start;
}
static std::size_t heap() {
    malloc_statistics_t value{};
    malloc_zone_statistics(nullptr, &value);
    return value.size_in_use;
}
int main(int argc, char **argv) {
    try {
        if (argc != 5) throw std::invalid_argument("count width batch retained");
        const auto count = std::stoull(argv[1]);
        const auto width = std::stoull(argv[2]);
        const auto batch = std::stoull(argv[3]);
        const auto retained = std::stoull(argv[4]);
        if (!count || count > 100000 || width < 34 || width > 200 || !batch || batch > 128 ||
            !retained || retained > 32) throw std::invalid_argument("bounds");
        std::vector<std::string> keys;
        keys.reserve(count);
        for (std::size_t i = 0; i != count; ++i) {
            auto key = "namespace0:service/feature_" + std::to_string(i);
            key.append(width - key.size(), 'x');
            keys.push_back(std::move(key));
        }
        const auto heap_start = heap();
        Map map;
        const auto load_cpu = measure([&] {
            for (std::size_t i = 0; i != count; ++i) {
                map = map.set(keys[i], i);
                map.collect(128);
            }
            while (map.reclamation_pending()) map.collect(128);
        });
        const auto heap_loaded = heap();
        std::uint64_t lookup_sum = 0;
        const auto lookup_cpu = measure([&] {
            for (std::size_t i = 0; i != 100000; ++i) {
                const auto index = (i * 7919) % count;
                const auto value = map.find(std::string_view(keys[index]));
                if (!value || *value != index) throw std::runtime_error("lookup mismatch");
                lookup_sum += *value;
            }
        });
        std::vector<Map> versions;
        versions.reserve(retained);
        const auto allocations_start = map.allocations();
        const auto update_cpu = measure([&] {
            for (std::size_t step = 0; step != 64; ++step) {
                versions.push_back(map);
                if (versions.size() > retained) versions.erase(versions.begin());
                for (std::size_t i = 0; i != batch; ++i) {
                    const auto index = (step * batch + i) % count;
                    map = map.set(keys[index], count + step);
                }
                map.collect(128);
            }
        });
        const auto heap_retained = heap();
        const auto retained_nodes = map.retained_nodes();
        const auto allocations = map.allocations() - allocations_start;
        std::uint64_t export_sum = 0;
        std::size_t rows = 0;
        const auto export_cpu = measure([&] {
            auto cursor = map.cursor();
            std::string_view previous;
            while (auto row = cursor.next()) {
                if (rows && previous >= *row->first) throw std::runtime_error("ordering");
                previous = *row->first;
                export_sum += *row->second;
                ++rows;
            }
        });
        if (rows != count) throw std::runtime_error("row count");
        std::size_t collected = 0, turns = 0;
        std::uint64_t max_turn = 0;
        const auto collect_cpu = measure([&] {
            versions.clear();
            map.retire_root();
            while (map.reclamation_pending()) {
                const auto start = Clock::now();
                const auto nodes = map.collect(128);
                max_turn = std::max(max_turn, std::uint64_t(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
                if (nodes > 128) throw std::runtime_error("collection budget");
                collected += nodes;
                ++turns;
            }
        });
        if (map.retained_nodes()) throw std::runtime_error("retained after drain");
        const auto heap_drained = heap();
        const auto final_cpu = measure([&] { Map empty; map = std::move(empty); });
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage)) throw std::runtime_error("rusage");
        std::printf("{\"count\":%llu,\"width\":%llu,\"batch\":%llu,\"retained\":%llu,"
                    "\"load_cpu_ns\":%llu,\"lookup_cpu_ns\":%llu,\"update_cpu_ns\":%llu,"
                    "\"export_cpu_ns\":%llu,\"collect_cpu_ns\":%llu,\"final_cpu_ns\":%llu,"
                    "\"heap_start\":%zu,\"heap_loaded\":%zu,\"heap_retained\":%zu,\"heap_drained\":%zu,"
                    "\"retained_nodes\":%zu,\"allocated_nodes\":%zu,\"collected_nodes\":%zu,"
                    "\"collection_turns\":%zu,\"max_collection_turn_ns\":%llu,"
                    "\"lookup_sum\":%llu,\"export_sum\":%llu,\"peak_rss_bytes\":%ld}\n",
                    count, width, batch, retained, (unsigned long long)load_cpu,
                    (unsigned long long)lookup_cpu, (unsigned long long)update_cpu,
                    (unsigned long long)export_cpu, (unsigned long long)collect_cpu,
                    (unsigned long long)final_cpu, heap_start, heap_loaded, heap_retained,
                    heap_drained, retained_nodes, allocations, collected, turns,
                    (unsigned long long)max_turn, (unsigned long long)lookup_sum,
                    (unsigned long long)export_sum, usage.ru_maxrss);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
