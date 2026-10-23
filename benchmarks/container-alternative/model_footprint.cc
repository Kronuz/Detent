#include <detent/model.hh>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <random>
#include <vector>
#include <ctime>
#include <memory>
#include <new>
#include <optional>
#include <string_view>

// --move measures final snapshot transfer; --move-pruned removes 3/4 first.

namespace {
bool metering = false;
std::size_t live = 0, peak = 0, calls = 0, requested = 0;
struct alignas(std::max_align_t) Allocation {
    void* base;
    std::size_t bytes;
    bool metered;
};
void* allocate(std::size_t bytes, std::size_t alignment) {
    alignment = std::max(alignment, alignof(Allocation));
    const auto extra = sizeof(Allocation) + alignment - 1;
    if (bytes > std::numeric_limits<std::size_t>::max() - extra) throw std::bad_alloc();
    const auto actual = std::max<std::size_t>(bytes, 1);
    auto base = std::malloc(actual + extra);
    if (!base) throw std::bad_alloc();
    auto cursor = static_cast<void*>(static_cast<char*>(base) + sizeof(Allocation));
    auto space = actual + alignment - 1;
    auto result = std::align(alignment, actual, cursor, space);
    if (!result) { std::free(base); throw std::bad_alloc(); }
    auto header = reinterpret_cast<Allocation*>(static_cast<char*>(result) - sizeof(Allocation));
    ::new (header) Allocation{base, bytes, metering};
    if (metering) { live += bytes; peak = std::max(peak, live); ++calls; requested += bytes; }
    return result;
}
void release(void* pointer) noexcept {
    if (!pointer) return;
    auto header = reinterpret_cast<Allocation*>(static_cast<char*>(pointer) - sizeof(Allocation));
    if (header->metered) live -= header->bytes;
    std::free(header->base);
}
}
void* operator new(std::size_t bytes) { return allocate(bytes, alignof(std::max_align_t)); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void* operator new(std::size_t bytes, std::align_val_t alignment) { return allocate(bytes, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }
void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { release(pointer); }
std::string knob_name(unsigned id, bool long_keys) {
    auto suffix = std::to_string(id);
    auto key = std::string(long_keys ? 200 : 7 + suffix.size(), 'k');
    key.replace(0, 6, "bench:");
    key.replace(key.size() - suffix.size(), suffix.size(), suffix);
    return key;
}
int main(int argc, char** argv) {
    unsigned count = 100000;
    std::string_view mode;
    std::string_view key_profile = "both";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--knobs" && index + 1 < argc) {
            const std::string_view value(argv[++index]);
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
            if (error != std::errc{} || end != value.data() + value.size() || count == 0) return 2;
        } else if (argument == "--key-profile" && index + 1 < argc) {
            key_profile = argv[++index];
            if (key_profile != "both" && key_profile != "short" && key_profile != "long") return 2;
        } else if ((argument == "--move" || argument == "--move-pruned") && mode.empty()) {
            mode = argument;
        } else return 2;
    }
    const bool transfer = !mode.empty(), prune = mode == "--move-pruned";
    for (const bool random_order : {false, true}) for (const bool long_keys : {false, true}) {
        if ((key_profile == "short" && long_keys) || (key_profile == "long" && !long_keys)) continue;
        std::vector<unsigned> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::mt19937 random(7106);
        if (random_order) std::shuffle(order.begin(), order.end(), random);
        live = peak = calls = requested = 0;
        std::size_t retained = 0;
        const auto start = std::clock();
        metering = true;
        {
            detent::Snapshot snapshot;
            for (unsigned i : order) {
                detent::BoolDef value;
                for (unsigned group = 0; group < 3; ++group) {
                    auto suffix = std::to_string(group);
                    auto key = std::string(long_keys ? 128 : 5 + suffix.size(), 'g');
                    key.replace(key.size() - suffix.size(), suffix.size(), suffix);
                    value.overrides.emplace(std::move(key), group % 2 == 0);
                }
                snapshot.knobs.emplace(knob_name(i, long_keys), std::move(value));
            }
            if (snapshot.knobs.size() != count) std::abort();
            if (prune)
                for (unsigned i = 0; i < count; ++i)
                    if (i % 4 && snapshot.knobs.erase(knob_name(i, long_keys)) != 1) std::abort();
            const unsigned expected = prune ? count / 4 + (count % 4 != 0) : count;
            std::optional<detent::Snapshot> published;
            if (transfer) {
                published.emplace(std::move(snapshot));
                snapshot.knobs.clear();
                if (published->knobs.size() != expected) std::abort();
            } else if (snapshot.knobs.size() != expected) std::abort();
            retained = live;
        }
        metering = false;
        if (live != 0) std::abort();
        const auto cpu = 1000.0 * (std::clock() - start) / CLOCKS_PER_SEC;
        std::printf("{\"workload\":\"model-%s-%s%s\",\"process_cpu_ms\":%.6f,\"knobs\":%u,\"overrides_per_knob\":3,\"long_keys\":%s,\"snapshot_object_bytes\":%zu,\"definition_object_bytes\":%zu,\"retained_requested_bytes\":%zu,\"peak_requested_bytes\":%zu,\"allocation_calls\":%zu,\"requested_bytes\":%zu,\"final_live_bytes\":%zu}\n", random_order ? "random" : "ordered", long_keys ? "long" : "short", prune ? "-pruned-moved" : transfer ? "-moved" : "", cpu, prune ? count / 4 + (count % 4 != 0) : count, long_keys ? "true" : "false", sizeof(detent::Snapshot), sizeof(detent::KnobDef), retained, peak, calls, requested, live);
    }
}
