#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <pthread.h>
#include <stdexcept>
#include <sys/resource.h>
#include <time.h>
namespace {
using Logger = void (*)(std::uint32_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                        std::uintptr_t, std::uint32_t);
std::atomic<pthread_t> caller{nullptr};
std::uint64_t *active_allocations = nullptr;
void allocation_event(std::uint32_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                      std::uintptr_t result, std::uint32_t) {
    if (pthread_equal(pthread_self(), caller.load(std::memory_order_acquire)) &&
        active_allocations && result)
        ++*active_allocations;
}
struct AllocationLog {
    Logger *slot;
    AllocationLog() : slot(static_cast<Logger *>(dlsym(RTLD_DEFAULT, "malloc_logger"))) {
        if (!slot || std::atomic_ref<Logger>(*slot).load())
            throw std::runtime_error("malloc logger unavailable or already occupied");
        caller.store(pthread_self(), std::memory_order_release);
        std::atomic_ref<Logger>(*slot).store(allocation_event, std::memory_order_release);
        std::uint64_t count = 0;
        active_allocations = &count;
        auto p = std::malloc(73);
        asm volatile("" : : "g"(p) : "memory");
        std::free(p);
        active_allocations = nullptr;
        if (count != 1) {
            std::atomic_ref<Logger>(*slot).store(nullptr);
            throw std::runtime_error("malloc logger smoke count differs");
        }
    }
    ~AllocationLog() {
        active_allocations = nullptr;
        std::atomic_ref<Logger>(*slot).store(nullptr);
    }
};
std::int64_t thread_cpu() {
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value))
        throw std::runtime_error("thread CPU clock failed");
    return value.tv_sec * 1000000000LL + value.tv_nsec;
}
std::int64_t process_cpu(const rusage &value) {
    return (value.ru_utime.tv_sec + value.ru_stime.tv_sec) * 1000000000LL +
           (value.ru_utime.tv_usec + value.ru_stime.tv_usec) * 1000;
}
struct Result {
    const char *name;
    std::int64_t wall_ns = 0, thread_ns = 0, process_ns = 0;
    long minor = 0, major = 0, voluntary = 0, involuntary = 0;
    std::uint64_t allocations = 0;
};
template <class F> Result phase(const char *name, F action) {
    Result result{name};
    rusage begin{}, end{};
    if (getrusage(RUSAGE_SELF, &begin)) throw std::runtime_error("rusage failed");
    auto cpu_begin = thread_cpu();
    active_allocations = &result.allocations;
    auto start = std::chrono::steady_clock::now();
    try { action(); } catch (...) { active_allocations = nullptr; throw; }
    auto stop = std::chrono::steady_clock::now();
    active_allocations = nullptr;
    auto cpu_end = thread_cpu();
    if (getrusage(RUSAGE_SELF, &end)) throw std::runtime_error("rusage failed");
    result.wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    result.thread_ns = cpu_end - cpu_begin;
    result.process_ns = process_cpu(end) - process_cpu(begin);
    result.minor = end.ru_minflt - begin.ru_minflt;
    result.major = end.ru_majflt - begin.ru_majflt;
    result.voluntary = end.ru_nvcsw - begin.ru_nvcsw;
    result.involuntary = end.ru_nivcsw - begin.ru_nivcsw;
    return result;
}
}
