#include <detent/model.hh>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <numeric>
#include <random>
namespace {
long budget = -1;
struct Allocation { void* pointer = nullptr; std::size_t bytes = 0; };
std::array<Allocation, 4096> allocations;
long live = 0;
}
void* operator new(std::size_t bytes) {
    if (budget == 0) throw std::bad_alloc();
    if (budget > 0) --budget;
    auto* pointer = std::malloc(bytes ? bytes : 1);
    if (!pointer) throw std::bad_alloc();
    for (auto& slot : allocations) if (!slot.pointer) { slot = {pointer, bytes}; ++live; return pointer; }
    std::abort();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept {
    if (!pointer) return;
    for (auto& slot : allocations) if (slot.pointer == pointer) { slot = {}; --live; std::free(pointer); return; }
    std::abort();
}
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void check(bool condition) { if (!condition) std::abort(); }
std::string name(unsigned id) {
    auto value = std::string(200, 'k');
    value.replace(0, 5, "test:");
    value.replace(194, 6, std::to_string(100000 + id));
    return value;
}
using Pair = std::pair<const std::string, std::optional<detent::KnobDef>>;
using Node = btree::btree_node<btree::btree_map_params<std::string, std::optional<detent::KnobDef>, std::less<>, std::allocator<Pair>, 256>>;
Node* owner(const Pair& value) {
    auto address = reinterpret_cast<std::uintptr_t>(&value);
    for (const auto& allocation : allocations) {
        auto begin = reinterpret_cast<std::uintptr_t>(allocation.pointer);
        if (allocation.pointer && address >= begin && address < begin + allocation.bytes)
            return std::launder(static_cast<Node*>(allocation.pointer));
    }
    std::abort();
}
int attempt(unsigned key, long limit) {
    detent::BoolDef definition;
    std::array<std::string, 3> groups;
    for (unsigned i = 0; i < 3; ++i) {
        groups[i] = std::string(128, 'g'); groups[i].back() = static_cast<char>('0' + i);
        definition.overrides.emplace(groups[i], i % 2 == 0);
    }
    detent::SnapshotPatch patch;
    std::array<unsigned, 40> order;
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 random(7106); std::shuffle(order.begin(), order.end(), random);
    for (auto id : order) patch.knobs.try_emplace(name(id * 2), definition);
    const auto incoming = name(key);
    auto cursor = patch.knobs.lower_bound(incoming);
    if (cursor == patch.knobs.end()) return -1;
    auto* node = owner(*cursor);
    if (!node->is_leaf() || node->count() == node->max_count()) return -1;
    const int count = node->count();
    auto* parent = node->get_parent();
    const auto before = live;
    budget = limit;
    bool refused = false;
    try { patch.knobs.try_emplace(incoming, definition); }
    catch (const std::bad_alloc&) { refused = true; }
    budget = -1;
    check(patch.knobs.size() == 40 + (refused ? 0 : 1));
    if (refused) check(live == before && node->count() == count && node->get_parent() == parent && parent->child(node->position()) == node);
    for (unsigned i = 0; i < 40; ++i) {
        auto found = patch.knobs.find(name(i * 2));
        check(found != patch.knobs.end() && found->second.has_value());
        const auto& actual = std::get<detent::BoolDef>(*found->second);
        check(actual.overrides.size() == 3);
        for (unsigned group = 0; group < 3; ++group)
            check(std::get<bool>(actual.overrides.at(groups[group])) == (group % 2 == 0));
    }
    patch.knobs.verify();
    return refused ? 0 : 1;
}
int main() {
    unsigned covered = 0;
    for (unsigned key = 1; key < 80; key += 2) {
        bool finished = false;
        for (long limit = 0; limit < 256; ++limit) {
            const auto before = live;
            const auto result = attempt(key, limit);
            check(live == before);
            if (result < 0) { finished = true; break; }
            if (result > 0) {
                std::printf("{\"key\":%u,\"refused_positions\":%ld,\"unreleased_allocations\":0}\n", key, limit);
                ++covered; finished = true; break;
            }
        }
        check(finished);
    }
    check(covered > 0);
    std::printf("{\"covered_nonfull_positions\":%u,\"final_live_allocations\":%ld}\n", covered, live);
}
