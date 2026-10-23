#include <detent/client.hh>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <numeric>
#include <random>
#include <string_view>
#include <vector>

namespace {
std::string name(unsigned id, bool missing, bool long_keys) {
    auto key = std::string(long_keys ? 200 : 16, 'k');
    const std::string_view prefix = missing ? "missing:" : "bench:";
    key.replace(0, prefix.size(), prefix);
    key.replace(key.size() - 6, 6, std::to_string(100000 + id));
    return key;
}
}
int main(int argc, char** argv) {
    bool long_keys = false, hot_keys = false;
    unsigned entries = 100000;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--long") long_keys = true;
        else if (argument == "--hot") hot_keys = true;
        else if (argument == "--knobs" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), entries);
            if (error != std::errc{} || end != value.data() + value.size() || entries < 2 || entries % 2) return 2;
        }
        else return 2;
    }
    constexpr unsigned operations = 200000, warmup = 10000;
    std::vector<std::string> names, misses;
    std::vector<unsigned> order(entries / 2);
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 random(7106);
    std::shuffle(order.begin(), order.end(), random);
    if (hot_keys) order.resize(std::min<std::size_t>(32, order.size()));
    std::string forced(long_keys ? 128 : 5, 'f');
    std::string denied(long_keys ? 128 : 5, 'd');
    std::string ramp(long_keys ? 128 : 6, 'r');
    detent::Snapshot snapshot;
    for (unsigned id = 0; id < entries; ++id) {
        names.push_back(name(id, false, long_keys));
        misses.push_back(name(id, true, long_keys));
        if (id % 2) {
            detent::BoolDef definition;
            definition.default_value = true;
            definition.overrides.emplace(forced, true);
            definition.overrides.emplace(denied, false);
            definition.overrides.emplace(ramp, detent::GroupRollout{50});
            snapshot.knobs.emplace(names.back(), std::move(definition));
        } else snapshot.knobs.emplace(names.back(), detent::IntDef{static_cast<std::int64_t>(id)});
    }
    detent::Client client(std::move(snapshot));
    auto view = client.read_view();
    auto run = [&](const char* label, auto&& operation) {
        for (unsigned i = 0; i < warmup; ++i) operation(order[i % order.size()]);
        std::uint64_t checksum = 0;
        const auto start = std::clock();
        for (unsigned i = 0; i < operations; ++i) checksum += operation(order[i % order.size()]);
        const double elapsed = 1000.0 * (std::clock() - start) / CLOCKS_PER_SEC;
        std::printf("{\"workload\":\"%s-%s%s\",\"knobs\":%u,\"query_keys\":%zu,\"operations\":%u,\"process_cpu_ms\":%.6f,\"checksum\":%llu}\n",
            label, long_keys ? "long" : "short", hot_keys ? "-hot32" : "", entries, order.size(), operations, elapsed,
            static_cast<unsigned long long>(checksum));
    };
    run("client-int-hit", [&](unsigned id) {
        auto result = client.get(names[2 * id]);
        if (!result || result.value() != 2 * id) std::abort();
        return static_cast<std::uint64_t>(result.value());
    });
    run("view-int-hit", [&](unsigned id) {
        auto result = view.get(names[2 * id]);
        if (!result || result.value() != 2 * id) std::abort();
        return static_cast<std::uint64_t>(result.value());
    });
    run("view-int-miss", [&](unsigned id) {
        auto result = view.get(misses[2 * id]);
        if (result) std::abort();
        return std::uint64_t{1};
    });
    run("view-bool-default", [&](unsigned id) {
        auto result = view.eval(names[2 * id + 1]);
        if (!result || !result.value()) std::abort();
        return std::uint64_t{1};
    });
    run("view-bool-forced", [&](unsigned id) {
        auto result = view.eval(names[2 * id + 1], {}, std::string_view(forced));
        if (!result || !result.value()) std::abort();
        return std::uint64_t{1};
    });
    run("view-bool-rollout", [&](unsigned id) {
        auto result = view.eval(names[2 * id + 1], std::string_view("host-123"), std::string_view(ramp));
        if (!result) std::abort();
        return static_cast<std::uint64_t>(result.value());
    });
}
