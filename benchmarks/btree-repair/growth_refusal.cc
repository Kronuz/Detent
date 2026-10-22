#include <detent/model.hh>
#include <cstdio>
#include <cstdlib>
#include <new>
namespace { int allocation_budget = -1; long live_allocations = 0; }
void* operator new(std::size_t bytes) {
    if (allocation_budget >= 0) std::fprintf(stderr, "allocation bytes=%zu budget=%d\n", bytes, allocation_budget);
    if (allocation_budget == 0) throw std::bad_alloc();
    if (allocation_budget > 0) --allocation_budget;
    if (auto pointer = std::malloc(bytes ? bytes : 1)) { ++live_allocations; return pointer; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { if (pointer) --live_allocations; std::free(pointer); }
void operator delete[](void* pointer) noexcept { if (pointer) --live_allocations; std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { if (pointer) --live_allocations; std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { if (pointer) --live_allocations; std::free(pointer); }
__attribute__((noinline)) int exercise() {
    detent::BoolDef definition;
    definition.overrides.emplace("canary", true);
    detent::SnapshotPatch patch;
    patch.knobs.try_emplace("test:one", definition);
    allocation_budget = 1;
    bool refused = false;
    try { patch.knobs.try_emplace("test:two"); }
    catch (const std::bad_alloc&) { refused = true; }
    allocation_budget = -1;
    std::printf("refused=%d size=%zu\n", refused, static_cast<std::size_t>(patch.knobs.size()));
    return refused && patch.knobs.size() == 1 ? 0 : 2;
}

int main() { const auto before = live_allocations; const auto result = exercise(); std::printf("unreleased_allocations=%ld\n", live_allocations - before); return result; }
