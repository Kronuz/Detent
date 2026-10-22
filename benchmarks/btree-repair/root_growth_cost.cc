#include <detent/model.hh>
#include <ctime>
#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>

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
int main() {
    constexpr unsigned repeats = 5000;
    for (const bool long_keys : {false, true}) {
        for (unsigned count : {1u, 2u, 4u}) {
            detent::BoolDef definition;
            for (unsigned group = 0; group < 3; ++group) {
                auto key = std::string(long_keys ? 128 : 6, 'g');
                key.back() = static_cast<char>('0' + group);
                definition.overrides.emplace(std::move(key), group % 2 == 0);
            }
            std::string keys[5];
            for (unsigned i = 0; i < 5; ++i) {
                keys[i] = std::string(long_keys ? 200 : 10, 'k');
                keys[i].replace(0, 5, "test:");
                keys[i].back() = static_cast<char>('0' + i);
            }
            live = peak = calls = requested = 0;
            std::size_t retained = 0;
            metering = true;
            auto start = std::clock();
            for (unsigned cycle = 0; cycle < repeats; ++cycle) {
                {
                    detent::SnapshotPatch patch;
                    for (unsigned i = 0; i < count; ++i)
                        patch.knobs.try_emplace(keys[i], definition);
                    patch.knobs.try_emplace(keys[count]);
                    if (patch.knobs.size() != count + 1) std::abort();
                    retained = live;
                }
                if (live != 0) std::abort();
            }
            const auto elapsed = 1000.0 * (std::clock() - start) / CLOCKS_PER_SEC;
            metering = false;
            std::printf("{\"workload\":\"root-%s-%u\",\"repeats\":%u,\"retained_requested_bytes\":%zu,\"peak_requested_bytes\":%zu,\"calls_per_cycle\":%zu,\"requested_bytes_per_cycle\":%zu,\"final_live_bytes\":%zu,\"process_cpu_ms\":%.6f}\n", long_keys ? "long" : "short", count, repeats, retained, peak, calls / repeats, requested / repeats, live, elapsed);
        }
    }
}
