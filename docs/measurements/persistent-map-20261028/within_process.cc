#include "persistent_map.hh"
#include <persistent_map.h>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <time.h>
#include <vector>

using Legacy = detent::authority::detail::PersistentMap<std::string, std::uint64_t>;
using Shared = persistent_map::PersistentMap<std::string, std::uint64_t>;
constexpr std::size_t count = 100000, steps = 1024, retained = 16, rounds = 12;
std::uint64_t cpu() {
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value)) throw std::runtime_error("clock");
    return std::uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
}
template <class Map> struct State {
    Map map;
    std::vector<Map> versions;
    explicit State(const std::vector<std::string> &keys) {
        versions.reserve(retained + 1);
        for (std::size_t i = 0; i != count; ++i) {
            map = map.set(keys[i], i);
            map.collect(128);
        }
        while (map.reclamation_pending()) map.collect(128);
    }
    void warm(const std::vector<std::string> &keys, std::size_t offset) const {
        for (std::size_t step = 0; step != steps; ++step) {
            auto value = map.find(std::string_view(keys[(offset + step) % count]));
            if (!value) throw std::runtime_error("warm lookup");
        }
    }
    void update(const std::vector<std::string> &keys, std::size_t offset) {
        for (std::size_t step = 0; step != steps; ++step) {
            versions.push_back(map);
            if (versions.size() > retained) versions.erase(versions.begin());
            const auto index = (offset + step) % count;
            map = map.set(keys[index], count + offset + step);
            if (map.collect(128) > 128) throw std::runtime_error("collection budget");
        }
    }
    void verify(const std::vector<std::string> &keys, std::size_t updated) const {
        if (map.size() != count) throw std::runtime_error("size");
        for (std::size_t i = 0; i != count; ++i) {
            auto value = map.find(std::string_view(keys[i]));
            if (!value || *value != (i < updated ? count + i : i))
                throw std::runtime_error("value");
        }
    }
};
struct Sample {
    unsigned round, arm, position;
    std::uint64_t cpu_ns;
    std::size_t allocations, nodes;
};
int main(int argc, char **argv) {
    try {
        if (argc != 2) throw std::invalid_argument("allocation order 0..2");
        const auto placement = std::stoul(argv[1]);
        if (placement > 2) throw std::invalid_argument("placement");
        std::vector<std::string> keys;
        keys.reserve(count);
        for (std::size_t i = 0; i != count; ++i) {
            auto key = "namespace0:service/feature_" + std::to_string(i);
            key.append(200 - key.size(), 'x');
            keys.push_back(std::move(key));
        }
        std::optional<State<Legacy>> a1, a2;
        std::optional<State<Shared>> b;
        for (unsigned i = 0; i != 3; ++i) {
            switch ((i + placement) % 3) {
            case 0: a1.emplace(keys); break;
            case 1: a2.emplace(keys); break;
            case 2: b.emplace(keys); break;
            }
        }
        a1->update(keys, 0); a2->update(keys, 0); b->update(keys, 0);
        constexpr std::array<std::array<unsigned, 3>, 6> orders{{
            {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};
        std::array<Sample, rounds * 3> samples{};
        for (unsigned round = 0; round != rounds; ++round) {
            const auto offset = (round + 1) * steps;
            for (unsigned position = 0; position != 3; ++position) {
                const auto arm = orders[round % orders.size()][position];
                auto take = [&](auto &state) {
                    state.warm(keys, offset);
                    const auto before = state.map.allocations();
                    const auto start = cpu();
                    state.update(keys, offset);
                    const auto elapsed = cpu() - start;
                    samples[round * 3 + position] = {round, arm, position, elapsed,
                        state.map.allocations() - before, state.map.retained_nodes()};
                };
                switch (arm) {
                case 0: take(*a1); break;
                case 1: take(*a2); break;
                case 2: take(*b); break;
                }
            }
            a1->verify(keys, offset + steps);
            a2->verify(keys, offset + steps);
            b->verify(keys, offset + steps);
            const auto &first = samples[round * 3];
            for (unsigned i = 1; i != 3; ++i) {
                const auto &other = samples[round * 3 + i];
                if (first.allocations != other.allocations || first.nodes != other.nodes)
                    throw std::runtime_error("node identity");
            }
        }
        for (const auto &sample : samples)
            std::printf("{\"placement\":%lu,\"round\":%u,\"arm\":%u,\"position\":%u,"
                        "\"update_cpu_ns\":%llu,\"updates\":%zu,\"allocated_nodes\":%zu,"
                        "\"retained_nodes\":%zu}\n", placement, sample.round, sample.arm,
                        sample.position, (unsigned long long)sample.cpu_ns, steps,
                        sample.allocations, sample.nodes);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
