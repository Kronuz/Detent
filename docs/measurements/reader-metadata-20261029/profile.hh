#pragma once
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <time.h>
#include <type_traits>
#include <utility>
#include <unistd.h>

namespace lookup_profile {
enum class File : unsigned { Selected, Storage, Image, Metadata, Lease, Directory, General, Count };
enum class Step : unsigned {
    Total, Open, Stat, Checks, Acl, Read, Close, Decode, Checksum, Lock, Named, Binding,
    Limits, Controls, Attempt, Retry, Count
};
struct Counters {
    std::uint64_t calls = 0, requested = 0, returned = 0, wall = 0, cpu = 0;
};
inline thread_local bool enabled = [] {
    auto value = std::getenv("METADATA_OBSERVER");
    return value && std::string_view(value) == "1";
}();
inline thread_local File current = File::General;
inline thread_local std::array<Counters, unsigned(File::Count) * unsigned(Step::Count)> counters{};
inline Counters &entry(File file, Step step) {
    return counters[unsigned(file) * unsigned(Step::Count) + unsigned(step)];
}
inline std::uint64_t cpu() noexcept {
    timespec time{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time)) std::abort();
    return std::uint64_t(time.tv_sec) * 1000000000 + time.tv_nsec;
}
struct Context {
    File previous = current;
    bool active = true;
    explicit Context(File file) { current = file; }
    void leave() noexcept { if (active) { current = previous; active = false; } }
    ~Context() { leave(); }
};
struct Scope {
    Counters *value = nullptr;
    std::chrono::steady_clock::time_point start{};
    std::uint64_t begin = 0;
    explicit Scope(Step step, std::size_t requested = 0) noexcept {
        const int saved = errno;
        if (enabled) {
            value = &entry(current, step);
            ++value->calls;
            value->requested += requested;
            begin = cpu();
            start = std::chrono::steady_clock::now();
        }
        errno = saved;
    }
    void returned(std::uint64_t bytes) noexcept { if (value) value->returned += bytes; }
    ~Scope() {
        const int saved = errno;
        if (value) {
            const auto end = std::chrono::steady_clock::now();
            const auto stop = cpu();
            value->wall += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            value->cpu += stop - begin;
        }
        errno = saved;
    }
};
template <class F> decltype(auto) call(Step step, F action) {
    Scope scope(step);
    return action();
}
template <class F> decltype(auto) call(File file, Step step, F action) {
    Context context(file);
    return call(step, std::move(action));
}
template <class F> auto read(std::size_t requested, F action) {
    Scope scope(Step::Read, requested);
    auto result = action();
    if (result > 0) scope.returned(static_cast<std::uint64_t>(result));
    return result;
}
inline void note(Step step) noexcept { if (enabled) ++entry(current, step).calls; }
template <class RecoveryFile> struct Close {
    RecoveryFile &file;
    File context = current;
    ~Close() {
        if (file.fd < 0) return;
        Context selected(context);
        Scope scope(Step::Close);
        ::close(std::exchange(file.fd, -1));
    }
};
inline void reset() noexcept { counters.fill({}); }
inline void dump() {
    constexpr std::array files{"selected", "storage", "image", "metadata", "lease", "directory", "general"};
    constexpr std::array steps{"total", "open", "stat", "checks", "acl", "read", "close", "decode", "checksum",
        "lock", "named", "binding", "limits", "controls", "attempt", "retry"};
    std::cerr << '[';
    bool first = true;
    for (unsigned file = 0; file != unsigned(File::Count); ++file) {
        for (unsigned step = 0; step != unsigned(Step::Count); ++step) {
            const auto &value = entry(File(file), Step(step));
            if (!value.calls) continue;
            if (!first) std::cerr << ',';
            first = false;
            std::cerr << "{\"file\":\"" << files[file] << "\",\"step\":\"" << steps[step]
                << "\",\"calls\":" << value.calls << ",\"requested_bytes\":" << value.requested
                << ",\"returned_bytes\":" << value.returned << ",\"wall_ns\":" << value.wall
                << ",\"caller_cpu_ns\":" << value.cpu << '}';
        }
    }
    std::cerr << "]\n";
}
}
