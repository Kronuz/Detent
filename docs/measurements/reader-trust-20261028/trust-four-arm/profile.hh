#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sys/resource.h>
#include <time.h>
namespace lookup_profile {
struct Event { const char *name; std::int64_t wall, cpu; long minor, major; };
inline thread_local bool enabled = false;
inline thread_local std::array<Event, 256> events{};
inline thread_local std::size_t count = 0;
inline std::int64_t cpu() {
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t)) std::abort();
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}
struct Scope {
    const char *name;
    rusage usage{};
    std::chrono::steady_clock::time_point start;
    std::int64_t begin;
    explicit Scope(const char *label) : name(enabled ? label : nullptr) {
        if (!name) return;
        if (getrusage(RUSAGE_SELF, &usage)) std::abort();
        begin = cpu(); start = std::chrono::steady_clock::now();
    }
    ~Scope() {
        if (!name) return;
        auto stop = std::chrono::steady_clock::now();
        auto end = cpu();
        rusage after{};
        if (getrusage(RUSAGE_SELF, &after) || count == events.size()) std::abort();
        events[count++] = {name, std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count(),
            end-begin, after.ru_minflt-usage.ru_minflt, after.ru_majflt-usage.ru_majflt};
    }
};
inline void dump() {
    std::cerr << "[";
    for (std::size_t i=0; i<count; ++i) {
        const auto &e=events[i];
        if (i) std::cerr << ',';
        std::cerr << "{\"step\":\"" << e.name << "\",\"wall_ns\":" << e.wall
                  << ",\"caller_cpu_ns\":" << e.cpu << ",\"minor_faults\":" << e.minor
                  << ",\"major_faults\":" << e.major << '}';
    }
    std::cerr << "]\n";
}
}
