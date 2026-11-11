// Process probes for the async-drivers measurements: live heap bytes (the allocator's
// usable size, so padding counts), open descriptors, threads, CPU time and a clock.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <new>
#include <sys/resource.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
inline std::size_t usable(void* p) { return malloc_size(p); }
#else
#include <malloc.h>
inline std::size_t usable(void* p) { return malloc_usable_size(p); }
#endif

namespace probe {
inline std::atomic<std::int64_t> live_bytes{0}, live_blocks{0}, total_blocks{0};
inline std::int64_t heap() { return live_bytes.load(std::memory_order_relaxed); }
inline std::int64_t blocks() { return live_blocks.load(std::memory_order_relaxed); }
inline std::int64_t allocations() { return total_blocks.load(std::memory_order_relaxed); }

inline int descriptors() {
	int count = 0;
	const int limit = static_cast<int>(sysconf(_SC_OPEN_MAX));
	for (int fd = 0; fd < limit && fd < (1 << 20); ++fd) { count += fcntl(fd, F_GETFD) != -1; }
	return count;
}
inline int threads() {
#if defined(__APPLE__)
	thread_act_array_t list; mach_msg_type_number_t count = 0;
	if (task_threads(mach_task_self(), &list, &count) != KERN_SUCCESS) { return -1; }
	for (mach_msg_type_number_t i = 0; i < count; ++i) { mach_port_deallocate(mach_task_self(), list[i]); }
	vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list), count * sizeof(thread_act_t));
	return static_cast<int>(count);
#else
	int count = 0;
	if (DIR* d = opendir("/proc/self/task")) { while (auto* e = readdir(d)) { count += e->d_name[0] != '.'; } closedir(d); }
	return count;
#endif
}
inline void raise_descriptor_limit() {
	rlimit r{}; getrlimit(RLIMIT_NOFILE, &r);
	rlim_t want = r.rlim_max == RLIM_INFINITY ? 65536 : r.rlim_max;
#if defined(__APPLE__)
	if (want > OPEN_MAX) { want = OPEN_MAX; }
#endif
	r.rlim_cur = want; setrlimit(RLIMIT_NOFILE, &r);
}
inline double cpu_seconds() {
	rusage u{}; getrusage(RUSAGE_SELF, &u);
	return u.ru_utime.tv_sec + u.ru_stime.tv_sec + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}
inline std::uint64_t now_ns() {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
// A snapshot of every counter, and the difference between two.
struct Sample { std::int64_t heap, blocks; int fds, threads; };
inline Sample sample() { return {heap(), blocks(), descriptors(), threads()}; }
}  // namespace probe

#define PROBE_ALLOCATOR                                                                              \
	void* operator new(std::size_t n) {                                                              \
		void* p = std::malloc(n ? n : 1);                                                            \
		if (!p) { throw std::bad_alloc(); }                                                          \
		probe::live_bytes.fetch_add(static_cast<std::int64_t>(usable(p)), std::memory_order_relaxed); \
		probe::live_blocks.fetch_add(1, std::memory_order_relaxed);                                  \
		probe::total_blocks.fetch_add(1, std::memory_order_relaxed);                                 \
		return p;                                                                                    \
	}                                                                                                \
	void* operator new[](std::size_t n) { return operator new(n); }                                  \
	void* operator new(std::size_t n, std::align_val_t a) {                                          \
		void* p = nullptr;                                                                           \
		if (posix_memalign(&p, static_cast<std::size_t>(a), n ? n : 1)) { throw std::bad_alloc(); }  \
		probe::live_bytes.fetch_add(static_cast<std::int64_t>(usable(p)), std::memory_order_relaxed); \
		probe::live_blocks.fetch_add(1, std::memory_order_relaxed);                                  \
		probe::total_blocks.fetch_add(1, std::memory_order_relaxed);                                 \
		return p;                                                                                    \
	}                                                                                                \
	void operator delete(void* p) noexcept {                                                         \
		if (!p) { return; }                                                                          \
		probe::live_bytes.fetch_sub(static_cast<std::int64_t>(usable(p)), std::memory_order_relaxed); \
		probe::live_blocks.fetch_sub(1, std::memory_order_relaxed);                                  \
		std::free(p);                                                                                \
	}                                                                                                \
	void operator delete[](void* p) noexcept { operator delete(p); }                                 \
	void operator delete(void* p, std::size_t) noexcept { operator delete(p); }                      \
	void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }                    \
	void operator delete(void* p, std::align_val_t) noexcept { operator delete(p); }                 \
	void operator delete(void* p, std::size_t, std::align_val_t) noexcept { operator delete(p); }
