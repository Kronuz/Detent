// The resume-side numbers docs/design/async-drivers.md relies on:
//   frame   bytes and create+destroy time of the frames a driver keeps alive
//   resume  one post+resume through inline_scheduler and asio_scheduler
//   wheel   bytes and time per timer entry: the proposed intrusive wheel (wheel.hh),
//           Kronuz/scheduler's SchedulerQueue, and one armed asio::steady_timer per group,
//           at 1, 1,000 and 1,000,000 entries
//   step    one idle Worker::run_one (today's loop calls it every 1 ms per group,
//           service.cc:1738 -> node.cc:557) and the CPU an idle group costs per second:
//           today's 1 ms timer loop against a wheel that wakes a group only at its deadline
#include "probe.hh"
PROBE_ALLOCATOR

#include "wheel.hh"

#include "completion/asio_completion.h"
#include "completion/native_completion.h"
#include "consensus/worker.h"
#include "scheduler.h"

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace j = kronuz::journal;
namespace c = kronuz::consensus;

namespace {
volatile std::uint64_t sink = 0;

template <class F>
double ns_per(std::size_t iterations, F&& f) {
	auto start = probe::now_ns();
	for (std::size_t i = 0; i < iterations; ++i) { f(i); }
	return static_cast<double>(probe::now_ns() - start) / static_cast<double>(iterations);
}
template <class F>
double best_of(int runs, F&& f) {
	double best = 1e300;
	for (int r = 0; r < runs; ++r) { best = std::min(best, f()); }
	return best;
}

// ---- frames -------------------------------------------------------------------------------
// A one-shot awaitable the owner completes, as a driver's IO or request wait does.
struct gate {
	std::coroutine_handle<> waiter;
	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<> h) noexcept { waiter = h; }
	void await_resume() const noexcept {}
};
// A request frame (propose/read/append waiting for its result): a few locals and one wait.
task::task<std::uint64_t> request_frame(gate& g, std::uint64_t request, std::string_view command) {
	std::uint64_t index = request * 3 + command.size();
	co_await g;
	co_return index;
}
// The per-core driver loop: one frame per reactor, never per group.
task::task<void> core_loop(gate& g, std::size_t& turns, bool& stop) {
	while (!stop) { co_await g; ++turns; }
}

// Frames are kept in a vector so the compiler cannot elide their allocation.
template <class Make>
void frame_row(const char* name, Make make) {
	std::vector<decltype(make())> kept;
	kept.reserve(1000);
	auto heap = probe::heap();
	auto blocks = probe::blocks();
	for (int i = 0; i < 1000; ++i) { kept.push_back(make()); }
	std::printf("frame=%s bytes=%.0f blocks=%.2f\n", name, (probe::heap() - heap) / 1000.0, (probe::blocks() - blocks) / 1000.0);
}

void frames() {
	gate g;
	std::size_t turns = 0; bool stop = false;
	frame_row("request", [&] { return request_frame(g, 7, "command"); });
	frame_row("core_loop", [&] { return core_loop(g, turns, stop); });
	{
		// The IO frame today: AsioCompletionDriver::run (asio_completion.h:105-202).
		asio::io_context io;
		task::asio_scheduler scheduler(io.get_executor());
		auto queue = std::make_shared<j::NativeCompletionQueue>();
		auto driver = std::make_shared<j::AsioCompletionDriver<j::NativeCompletionQueue>>(scheduler, queue, false);
		frame_row("asio_completion_run", [&] { return driver->run(nullptr); });
	}
	std::vector<task::task<std::uint64_t>> kept(1000);
	const double create = best_of(5, [&] {
		return ns_per(200000, [&](std::size_t i) { kept[i % 1000] = request_frame(g, i, "x"); });
	});
	std::printf("frame=request create_destroy_ns=%.1f pool=off\n", create);
}

// ---- resume -------------------------------------------------------------------------------
struct resumer {
	task::scheduler_ref s;
	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<> h) const { s.post(h); }
	void await_resume() const noexcept {}
};
task::task<void> bounce(task::scheduler_ref s, std::size_t n) {
	for (std::size_t i = 0; i < n; ++i) { co_await resumer{s}; }
}
void resumes() {
	constexpr std::size_t n = 1000000;
	{
		task::inline_scheduler s;
		const double ns = best_of(5, [&] {
			auto start = probe::now_ns();
			task::run(s, bounce(task::scheduler_ref::of(s), n));
			return static_cast<double>(probe::now_ns() - start) / n;
		});
		std::printf("resume=inline_scheduler ns=%.1f\n", ns);
	}
	{
		asio::io_context io;
		task::asio_scheduler s(io.get_executor());
		const double ns = best_of(5, [&] {
			io.restart();
			auto start = probe::now_ns();
			task::spawn(task::scheduler_ref::of(s), bounce(task::scheduler_ref::of(s), n));
			io.run();
			return static_cast<double>(probe::now_ns() - start) / n;
		});
		std::printf("resume=asio_scheduler ns=%.1f\n", ns);
	}
}

// ---- wheel --------------------------------------------------------------------------------
struct noop_scheduler {};
struct entry_task;
using SQueue = SchedulerQueue<noop_scheduler, entry_task, ThreadPolicyType::regular>;
struct entry_task : ScheduledTask<noop_scheduler, entry_task, ThreadPolicyType::regular> {
	std::uint64_t group = 0;
	void operator()() { sink += group; }
};

void wheels(std::size_t n) {
	std::mt19937_64 random(n);
	const std::uint64_t base = probe::now_ns();
	auto deadline = [&](std::uint64_t spread_ms) { return base + (1 + random() % spread_ms) * 1'000'000; };
	// The proposed wheel: entries live in the groups; one wheel per reactor.
	{
		auto heap = probe::heap();
		auto w = std::make_unique<bench::wheel>(base);
		std::vector<bench::wheel_entry> entries(n);
		const auto added = probe::heap() - heap - static_cast<std::int64_t>(n * sizeof(bench::wheel_entry));
		const double arm = ns_per(n, [&](std::size_t i) { w->arm(entries[i], deadline(2000)); });
		const std::size_t loops = std::max<std::size_t>(n, 1000000);
		const double rearm = best_of(5, [&] { return ns_per(loops, [&](std::size_t i) { w->arm(entries[i % n], deadline(2000)); }); });
		std::size_t fired = 0;
		auto start = probe::now_ns();
		for (std::uint64_t t = base; fired < n; t += 1'000'000) { fired += w->expire(t, [](bench::wheel_entry&) {}); }
		const double expire = static_cast<double>(probe::now_ns() - start) / static_cast<double>(n);
		std::printf("wheel=intrusive entries=%zu entry_bytes=%zu heap_per_entry=0 wheel_sizeof=%zu wheel_heap_bytes=%lld arm_ns=%.1f rearm_ns=%.1f expire_ns=%.1f\n",
			n, sizeof(bench::wheel_entry), sizeof(bench::wheel), static_cast<long long>(added), arm, rearm, expire);
	}
	// Kronuz/scheduler's wheel (scheduler.h:199-311): a shared_ptr task per entry; a re-arm
	// clears the old task, which stays filed until walked (scheduler.h:181-191).
	{
		auto heap = probe::heap();
		auto q = std::make_unique<SQueue>();
		const auto empty = probe::heap() - heap;
		std::vector<std::shared_ptr<entry_task>> live(n);
		heap = probe::heap();
		const double arm = ns_per(n, [&](std::size_t i) {
			live[i] = std::make_shared<entry_task>();
			live[i]->group = i;
			q->add(live[i], std::chrono::steady_clock::time_point(std::chrono::nanoseconds(deadline(2000))));
		});
		const auto armed = probe::heap() - heap;
		heap = probe::heap();
		const double rearm = ns_per(n, [&](std::size_t i) {
			live[i]->clear();
			live[i] = std::make_shared<entry_task>();
			live[i]->group = i;
			q->add(live[i], std::chrono::steady_clock::time_point(std::chrono::nanoseconds(deadline(2000))));
		});
		const auto rearmed = probe::heap() - heap;
		std::printf("wheel=scheduler_queue entries=%zu entry_sizeof=%zu queue_bytes=%lld heap_per_entry=%.0f heap_per_rearm=%.0f arm_ns=%.1f rearm_ns=%.1f\n",
			n, sizeof(entry_task), static_cast<long long>(empty), static_cast<double>(armed) / n, static_cast<double>(rearmed) / n, arm, rearm);
		live.clear();
	}
	// One armed asio::steady_timer per group (what task::deadline does, task_asio.hh:130-136).
	{
		asio::io_context io;
		auto heap = probe::heap();
		std::vector<asio::steady_timer> timers;
		timers.reserve(n);
		for (std::size_t i = 0; i < n; ++i) { timers.emplace_back(io); }
		const auto objects = probe::heap() - heap;
		heap = probe::heap();
		const double arm = ns_per(n, [&](std::size_t i) {
			timers[i].expires_at(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(deadline(2000))));
			timers[i].async_wait([](const asio::error_code&) {});
		});
		const auto armed = probe::heap() - heap;
		const double rearm = ns_per(n, [&](std::size_t i) {
			timers[i].expires_at(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(deadline(2000))));
			timers[i].async_wait([](const asio::error_code&) {});
		});
		io.poll();  // reap the cancelled waits
		std::printf("wheel=asio_steady_timer entries=%zu timer_sizeof=%zu heap_per_timer=%.0f heap_per_armed_wait=%.0f arm_ns=%.1f rearm_ns=%.1f\n",
			n, sizeof(asio::steady_timer), static_cast<double>(objects) / n, static_cast<double>(armed) / n, arm, rearm);
		for (auto& t : timers) { t.cancel(); }
		io.poll();
	}
}

// ---- step ---------------------------------------------------------------------------------
const j::AdmissionLimits admission{{1024ull * 1024 * 1024, 65536}, {8ull * 1024 * 1024, 3}, {512ull * 1024 * 1024, 4}, 64, 3};

std::unique_ptr<c::Worker> idle_worker(const fs::path& dir, std::vector<c::NodeId> voters) {
	fs::create_directories(dir);
	fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace);
	c::FixedConfiguration config; config.local = 1; config.voters = voters; config.cluster[0] = 'S'; config.configuration[0] = static_cast<char>(voters.size());
	c::WorkerLimits scheduling; const c::Limits defaults;
	scheduling.retained = {defaults.command_bytes, defaults.log_bytes, defaults.log_entries, defaults.rpc_bytes, defaults.rpc_entries};
	auto worker = std::make_unique<c::Worker>(std::make_shared<j::PosixIO>(dir), config, admission, c::Limits{}, c::Timing{}, scheduling);
	j::Identity id{}; id[0] = 'S'; id[1] = static_cast<char>(voters.size());
	worker->create(id);
	auto pump = [&](c::Tick tick) {
		for (int i = 0; i < 64; ++i) {
			auto result = worker->run_one(tick);
			while (auto op = worker->take_io_operation()) { j::drive_synchronously(*op); result = worker->run_one(tick); }
			for (const auto& action : worker->take_actions()) {
				if (auto committed = std::get_if<c::Committed>(&action)) { worker->applied({committed->entries.back().index}); }
			}
			if (result == c::TurnResult::Fenced) { throw std::runtime_error("worker fenced"); }
		}
	};
	pump({1, 1500});
	if (worker->try_submit(c::Start{}) != c::SubmitResult::Accepted) { throw std::runtime_error("start refused"); }
	pump({2, 1500});
	if (voters.size() == 1) { pump({2000, 1500}); }
	return worker;
}

void steps(const fs::path& dir) {
	// Today's loop: one run_one per group per 1 ms wake, the clock advancing 1 ms per call.
	for (auto voters : {std::vector<c::NodeId>{1}, std::vector<c::NodeId>{1, 2, 3}}) {
		auto worker = idle_worker(dir / ("step-" + std::to_string(voters.size())), voters);
		std::uint64_t now = 2000;
		auto allocations = probe::allocations();
		constexpr std::size_t n = 200000;
		const double ns = best_of(5, [&] {
			return ns_per(n, [&](std::size_t) {
				// A follower stays a follower: its clock stays below the election deadline.
				const std::uint64_t at = voters.size() == 1 ? ++now : 2 + (now++ % 500);
				sink += static_cast<std::uint64_t>(worker->run_one({at, 1500}));
				for (auto& action : worker->take_actions()) { sink += action.index(); }
			});
		});
		std::printf("step=worker_run_one voters=%zu role=%s ns=%.1f allocations_per_call=%.3f\n", voters.size(),
			worker->role() == c::Role::Leader ? "leader" : "follower", ns,
			static_cast<double>(probe::allocations() - allocations) / (5.0 * n));
	}
}

// CPU per idle group per second: K groups whose only work is their timer, for `seconds`.
// today: each group's loop re-arms a 1 ms steady_timer (service.cc:2603-2605).
// wheel: the groups' deadlines in one wheel (leader cadence 100 ms, follower 1-2 s), and one
// steady_timer armed at the wheel's next slot.
void idle_cpu(std::size_t groups, double seconds) {
	{
		asio::io_context io;
		std::vector<std::unique_ptr<asio::steady_timer>> timers;
		std::uint64_t wakes = 0;
		std::function<void(std::size_t)> arm = [&](std::size_t i) {
			timers[i]->expires_after(std::chrono::milliseconds(1));
			timers[i]->async_wait([&, i](const asio::error_code& e) { if (!e) { ++wakes; arm(i); } });
		};
		for (std::size_t i = 0; i < groups; ++i) { timers.push_back(std::make_unique<asio::steady_timer>(io)); arm(i); }
		const double cpu = probe::cpu_seconds();
		io.run_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000)));
		const double used = probe::cpu_seconds() - cpu;
		std::printf("idle=today_1ms_loop groups=%zu seconds=%.1f wakes_per_group_s=%.0f cpu_us_per_group_s=%.2f\n", groups, seconds,
			wakes / (groups * seconds), used * 1e6 / (groups * seconds));
	}
	for (std::uint64_t cadence_ms : {std::uint64_t{100}, std::uint64_t{1500}}) {
		asio::io_context io;
		asio::steady_timer timer(io);
		bench::wheel w(probe::now_ns());
		std::vector<bench::wheel_entry> entries(groups);
		std::mt19937_64 random(groups);
		const auto start = probe::now_ns();
		for (auto& e : entries) { w.arm(e, start + (1 + random() % cadence_ms) * 1'000'000); }
		std::uint64_t fires = 0, wakes = 0;
		std::function<void()> schedule = [&] {
			const auto next = w.next();
			timer.expires_at(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(next)));
			timer.async_wait([&](const asio::error_code& e) {
				if (e) { return; }
				++wakes;
				const auto now = probe::now_ns();
				fires += w.expire(now, [&](bench::wheel_entry& entry) { w.arm(entry, now + cadence_ms * 1'000'000); });
				schedule();
			});
		};
		schedule();
		const double cpu = probe::cpu_seconds();
		io.run_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000)));
		const double used = probe::cpu_seconds() - cpu;
		std::printf("idle=wheel cadence_ms=%llu groups=%zu seconds=%.1f fires_per_group_s=%.2f reactor_wakes_s=%.0f cpu_us_per_group_s=%.3f\n",
			static_cast<unsigned long long>(cadence_ms), groups, seconds, fires / (groups * seconds), wakes / seconds, used * 1e6 / (groups * seconds));
	}
}
}  // namespace

int main(int argc, char** argv) {
	const std::string only = argc > 1 ? argv[1] : "all";
	const fs::path dir = std::getenv("BENCH_DIR") ? std::getenv("BENCH_DIR") : "bench-data";
	fs::remove_all(dir / "steps");
	if (only == "all" || only == "frame") { frames(); }
	if (only == "all" || only == "resume") { resumes(); }
	if (only == "all" || only == "wheel") { for (std::size_t n : {std::size_t{1}, std::size_t{1000}, std::size_t{1000000}}) { wheels(n); } }
	if (only == "all" || only == "step") { steps(dir / "steps"); }
	if (only == "all" || only == "idle") { for (std::size_t n : {std::size_t{1}, std::size_t{1000}}) { idle_cpu(n, 3.0); } }
	fs::remove_all(dir / "steps");
	return 0;
}
