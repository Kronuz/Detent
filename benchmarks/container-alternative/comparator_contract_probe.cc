#include "detent/flat_map.hh"
#include <algorithm>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

struct SwapCompare {
    bool reverse = false;
    inline static bool refuse = false;
    bool operator()(int a, int b) const noexcept { return reverse ? a > b : a < b; }
    friend void swap(SwapCompare& a, SwapCompare& b) {
        std::swap(a.reverse, b.reverse);
        if (refuse) throw 1;
    }
};
struct MoveCompare {
    inline static bool refuse = false;
    MoveCompare() = default;
    MoveCompare(const MoveCompare&) = default;
    MoveCompare& operator=(const MoveCompare&) = default;
    MoveCompare(MoveCompare&&) { if (refuse) throw 1; }
    MoveCompare& operator=(MoveCompare&&) { if (refuse) throw 1; return *this; }
    bool operator()(int a, int b) const noexcept { return a < b; }
};
template<class Map> void assignment() {
    if constexpr (!std::is_copy_assignable_v<Map>) {
        std::cout << "copy assignment disabled for throwing comparator swap\n";
    } else {
        Map target(SwapCompare{false}), source(SwapCompare{true});
        target.try_emplace(1, 1); target.try_emplace(2, 2);
        source.try_emplace(20, 20); source.try_emplace(10, 10);
        SwapCompare::refuse = true;
        try { target = source; } catch (int) {}
        auto comp = target.key_comp();
        const bool ordered = std::is_sorted(target.begin(), target.end(), [&](const auto& a, const auto& b) {
            return comp(a.first, b.first);
        });
        std::cout << "target keys=" << target.begin()->first << ',' << std::next(target.begin())->first
                  << " reverse=" << comp.reverse << " ordered=" << ordered << '\n';
    }
}
int main(int argc, char**) {
    if (argc == 1) { assignment<detent::flat_map<int, int, SwapCompare>>(); return 0; }
    using Map = detent::flat_map<int, int, MoveCompare>;
    std::cout << "nothrow_move=" << std::is_nothrow_move_constructible_v<Map> << std::endl;
    Map source; source.try_emplace(1, 1);
    MoveCompare::refuse = true;
    try { Map moved(std::move(source)); } catch (int) { std::cout << "move exception caught\n"; }
}
