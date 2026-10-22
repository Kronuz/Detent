#include <detent/model.hh>
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
    for (const bool long_keys : {false, true}) {
        live = peak = calls = requested = 0;
        std::size_t retained = 0;
        metering = true;
        {
            detent::Snapshot snapshot;
            for (unsigned i = 0; i < 100000; ++i) {
                detent::BoolDef value;
                for (unsigned group = 0; group < 3; ++group) {
                    auto suffix = std::to_string(group);
                    auto key = std::string(long_keys ? 128 : 5 + suffix.size(), 'g');
                    key.replace(key.size() - suffix.size(), suffix.size(), suffix);
                    value.overrides.emplace(std::move(key), group % 2 == 0);
                }
                auto suffix = std::to_string(i);
                auto key = std::string(long_keys ? 200 : 7 + suffix.size(), 'k');
                key.replace(0, 6, "bench:");
                key.replace(key.size() - suffix.size(), suffix.size(), suffix);
                snapshot.knobs.emplace(std::move(key), std::move(value));
            }
            if (snapshot.knobs.size() != 100000) std::abort();
            retained = live;
        }
        metering = false;
        if (live != 0) std::abort();
        std::printf("{\"knobs\":100000,\"overrides_per_knob\":3,\"long_keys\":%s,\"snapshot_object_bytes\":%zu,\"definition_object_bytes\":%zu,\"retained_requested_bytes\":%zu,\"peak_requested_bytes\":%zu,\"allocation_calls\":%zu,\"requested_bytes\":%zu,\"final_live_bytes\":%zu}\n", long_keys ? "true" : "false", sizeof(detent::Snapshot), sizeof(detent::KnobDef), retained, peak, calls, requested, live);
    }
}
