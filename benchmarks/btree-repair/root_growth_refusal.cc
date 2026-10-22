#include <detent/model.hh>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
namespace { long allocation_budget = -1, live_allocations = 0; }
void* operator new(std::size_t bytes) {
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
void check(bool condition) { if (!condition) std::abort(); }
std::string name(char fill, bool long_keys) {
    auto value = std::string(long_keys ? 200 : 10, fill);
    value.replace(0, 5, "test:");
    return value;
}
__attribute__((noinline)) bool attempt(unsigned shape, long budget) {
    const bool long_keys = shape != 0;
    const unsigned count = shape == 2 ? 2 : 1;
    const auto first = name('c', long_keys), second = name('z', long_keys);
    const auto incoming = name(shape == 0 ? 'z' : shape == 1 ? 'a' : 'm', long_keys);
    std::array<std::string, 3> groups;
    detent::BoolDef definition;
    for (unsigned group = 0; group < 3; ++group) {
        groups[group] = std::string(long_keys ? 128 : 6, 'g');
        groups[group].back() = static_cast<char>('0' + group);
        definition.overrides.emplace(groups[group], group % 2 == 0);
    }
    detent::SnapshotPatch patch;
    patch.knobs.try_emplace(first, definition);
    if (count == 2) patch.knobs.try_emplace(second, definition);
    allocation_budget = budget;
    bool refused = false;
    try { patch.knobs.try_emplace(incoming); }
    catch (const std::bad_alloc&) { refused = true; }
    allocation_budget = -1;
    check(patch.knobs.size() == count + (refused ? 0 : 1));
    for (const auto* key : {&first, count == 2 ? &second : &first}) {
        const auto found = patch.knobs.find(*key);
        check(found != patch.knobs.end() && found->second.has_value());
        const auto& value = std::get<detent::BoolDef>(*found->second);
        check(value.overrides.size() == 3);
        for (unsigned group = 0; group < 3; ++group)
            check(std::get<bool>(value.overrides.at(groups[group])) == (group % 2 == 0));
    }
    return refused;
}
int main() {
    for (unsigned shape = 0; shape < 3; ++shape) {
        bool complete = false;
        for (long budget = 0; budget < 64; ++budget) {
            const auto before = live_allocations;
            const bool refused = attempt(shape, budget);
            check(live_allocations == before);
            if (!refused) {
                std::printf("{\"shape\":%u,\"refused_positions\":%ld,\"unreleased_allocations\":0}\n", shape, budget);
                complete = true;
                break;
            }
        }
        check(complete);
    }
}
