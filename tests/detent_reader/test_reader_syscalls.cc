// T1's no-syscall and no-allocation checks on warm lookups (reader.md D3, T1).
// Linux: a forked child installs a seccomp filter that allows only `write` on its result pipe and
// `exit`/`exit_group`, runs 1M lookups and reports. macOS: the task's BSD syscall count over 1M
// lookups. Both: the allocation probe counts zero allocations across 1M lookups.
#include "detent/reader.h"
#include "detent_testing/generation_writer.hh"
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#if defined(__linux__)
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace detent::test {
std::atomic<bool> counting{false};
std::atomic<std::size_t> allocations{0};
struct AllocationProbe {
	static void allocate(std::size_t) {
		if (counting.load(std::memory_order_relaxed))
			allocations.fetch_add(1, std::memory_order_relaxed);
	}
};
} // namespace detent::test
#include "support/allocation_probe.hh"

namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("reader syscall invariant at line " + std::to_string(line));
}
#define CHECK(value) check(value, __LINE__)
detent_str S(std::string_view s) { return {s.data(), s.size()}; }
constexpr detent_str none{nullptr, 0};
constexpr std::size_t calls = 1'000'000;
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define DETENT_SANITIZED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define DETENT_SANITIZED 1
#endif

// 1M lookups across get, eval with a hash and a switch, and read; returns the sum of the answers.
std::int64_t lookups(detent_reader *h) {
	std::int64_t total = 0;
	for (std::size_t i = 0; i != calls; ++i) {
		std::int64_t value = 0;
		bool flag = false;
		detent_value read{};
		switch (i % 3) {
		case 0:
			if (detent_get(h, S("t:rev"), 0, &value) != DETENT_OK)
				return -1;
			total += value;
			break;
		case 1:
			if (detent_eval(h, S("t:flag"), S("user"), S("vip"), false, &flag) != DETENT_OK)
				return -1;
			total += flag;
			break;
		default:
			if (detent_read(h, S("u:ramp"), S("user"), none, &read) != DETENT_OK)
				return -1;
			total += read.boolean;
		}
	}
	return total;
}
} // namespace
int main() {
	const auto root = std::filesystem::current_path() / ".scratch" / ("reader-syscalls-" + std::to_string(::getpid()));
	std::filesystem::remove_all(root);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};
	{
		// The writer's store threads end before anything is counted.
		detent::testing::GenerationWriter writer(root);
		CHECK(writer.publish_json(R"({"schema_version":1,"knobs":{"t:rev":{"type":"int","default":3},)"
		                          R"("t:flag":{"type":"bool","default":false,"overrides":{"vip":true}},)"
		                          R"("u:ramp":{"type":"bool","default":false,"rollout_pct":100}}})") == 1);
	}
	const auto path = root.string();
	detent_open_options options{sizeof options, S(path), nullptr, 0, uint32_t(::getuid())};
	detent_reader *handle = nullptr;
	CHECK(detent_open(&options, &handle) == DETENT_OK);
	// Warm: attach, map and verify every page on the paths.
	CHECK(lookups(handle) > 0);
	detent::test::counting = true;
	const auto total = lookups(handle);
	detent::test::counting = false;
	// get 3 for every i % 3 == 0, eval true and read true for the others.
	CHECK(total == std::int64_t((calls + 2) / 3) * 3 + std::int64_t(calls - (calls + 2) / 3));
	std::cout << "allocations in " << calls << " warm lookups: " << detent::test::allocations.load() << "\n";
	CHECK(detent::test::allocations.load() == 0);
#if defined(__linux__)
	int result[2];
	CHECK(::pipe(result) == 0);
	const pid_t pid = ::fork();
	CHECK(pid >= 0);
	if (pid == 0) {
		::close(result[0]);
		const auto fd = std::uint32_t(result[1]);
		sock_filter filter[] = {
		    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
		    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_exit_group, 0, 1),
		    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_exit, 0, 1),
		    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_write, 1, 0),
		    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL),
		    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0])),
		    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, fd, 0, 1),
		    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL),
		};
		sock_fprog program{static_cast<unsigned short>(sizeof filter / sizeof filter[0]), filter};
		if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 || ::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0)
			std::_Exit(2);
		const std::int64_t answer = lookups(handle);
		if (::write(result[1], &answer, sizeof answer) != sizeof answer)
			std::_Exit(3);
		std::_Exit(0);
	}
	::close(result[1]);
	std::int64_t answer = -2;
	const auto got = ::read(result[0], &answer, sizeof answer);
	int status = 0;
	CHECK(::waitpid(pid, &status, 0) == pid);
	std::cout << "seccomp child: " << (WIFEXITED(status) ? "exit " + std::to_string(WEXITSTATUS(status))
	                                                    : "signal " + std::to_string(WTERMSIG(status)))
	          << "\n";
#ifndef DETENT_SANITIZED
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0 && got == sizeof answer && answer == total);
#else
	(void)got;
#endif
#elif defined(__APPLE__)
	task_events_info before{}, after{};
	mach_msg_type_number_t count = TASK_EVENTS_INFO_COUNT;
	CHECK(task_info(mach_task_self(), TASK_EVENTS_INFO, task_info_t(&before), &count) == KERN_SUCCESS);
	const auto again = lookups(handle);
	count = TASK_EVENTS_INFO_COUNT;
	CHECK(task_info(mach_task_self(), TASK_EVENTS_INFO, task_info_t(&after), &count) == KERN_SUCCESS);
	CHECK(again == total);
	const auto bsd = after.syscalls_unix - before.syscalls_unix;
	std::cout << "BSD syscalls in " << calls << " warm lookups: " << bsd << "\n";
#ifndef DETENT_SANITIZED
	CHECK(bsd == 0);
#endif
#endif
	detent_close(handle);
	std::cout << "ok\n";
	return 0;
}
