#pragma once
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>
namespace ancestor_trace {
struct Event {
    const char *kind = nullptr;
    std::array<char, 65> name{};
    int flags = 0, status = 0, error = 0;
};
inline thread_local bool enabled = false;
inline thread_local std::array<Event, 256> events{};
inline thread_local std::size_t count = 0;
inline void reset() { events.fill({}); count = 0; }
template <class F> auto call(const char *kind, const char *name, int flags, F action) {
    auto result = action();
    const int saved = errno;
    if (enabled) {
        const auto size = std::strlen(name);
        if (count == events.size() || size > 64) std::abort();
        auto &event = events[count++];
        event.kind = kind;
        std::copy_n(name, size, event.name.begin());
        event.flags = flags;
        if constexpr (std::is_same_v<decltype(result), bool>) event.status = result ? 1 : 0;
        else event.status = result < 0 ? -1 : 1;
        event.error = event.status < 0 ? saved : 0;
    }
    errno = saved;
    return result;
}
inline void dump() {
    std::cerr << '[';
    for (std::size_t i = 0; i != count; ++i) {
        if (i) std::cerr << ',';
        const auto &event = events[i];
        std::cerr << "{\"kind\":\"" << event.kind << "\",\"name\":\"" << event.name.data()
            << "\",\"flags\":" << event.flags << ",\"status\":" << event.status
            << ",\"error\":" << event.error << '}';
    }
    std::cerr << "]\n";
}
}
