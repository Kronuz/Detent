#include "trace.hh"
#include "detent/client.hh"
#include "detent_local/client_source.hh"
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <optional>
#include <pthread.h>
#include <stdexcept>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

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
int main(int argc, char **argv) {
    try {
        if ((argc != 5 && argc != 6) || std::string_view(argv[1]) != "--read" ||
            (argc == 6 && std::string_view(argv[5]) != "--allocations"))
            throw std::invalid_argument("use --read DIR KEY EXPECTED_INT");
        std::int64_t expected = 0;
        auto input = std::string_view(argv[4]);
        auto [tail, error] = std::from_chars(input.data(), input.data()+input.size(), expected);
        if (error != std::errc{} || tail != input.data()+input.size())
            throw std::invalid_argument("invalid expected integer");
        std::optional<AllocationLog> allocation_log;
        if (argc == 6) allocation_log.emplace();
        (void)thread_cpu();
        ancestor_trace::enabled = false;
        ancestor_trace::reset();
        std::optional<detent::InitOptions> options;
        std::optional<detent::Client> client;
        auto factory = phase("factory", [&] {
            options.emplace();
            options->read_source = detent::local::make_managed_read_source(argv[2], getuid());
        });
        ancestor_trace::enabled = true;
        auto construct = phase("client", [&] { client.emplace(std::move(*options)); });
        ancestor_trace::enabled = false;
        auto lookup = phase("lookup", [&] {
            auto result = client->get(argv[3]);
            if (!result || result.value() != expected) throw std::runtime_error("value differs");
        });
        if (client->health().bool_knobs + client->health().int_knobs != 5000000)
            throw std::runtime_error("cardinality differs");
        std::cout << "{\"records\":5000000,\"diagnostic_only\":true,\"allocation_logging\":"
                  << (allocation_log ? "true" : "false") << ",\"phases\":[";
        bool first = true;
        for (const auto &result : {factory, construct, lookup}) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"phase\":\"" << result.name << "\",\"wall_ns\":" << result.wall_ns
                      << ",\"caller_cpu_ns\":" << result.thread_ns << ",\"process_cpu_ns\":" << result.process_ns
                      << ",\"minor_faults\":" << result.minor << ",\"major_faults\":" << result.major
                      << ",\"voluntary_switches\":" << result.voluntary
                      << ",\"involuntary_switches\":" << result.involuntary
                      << ",\"caller_allocation_events\":";
            if (allocation_log) std::cout << result.allocations; else std::cout << "null";
            std::cout << '}';
        }
        std::cout << "]}\n";
        ancestor_trace::dump();
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
