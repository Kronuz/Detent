// Xapiand review 2, X5: what a node-pair connection pays to move received frames to their
// groups' loops (D10). Loop A, which owns the connection, has decoded frames of 64 bytes; it
// delivers them to loop B in batches of k, one asio::post per batch, as the reader posts each
// destination loop's batch once per socket read. Two asio::io_contexts on two threads.
//   busy  A posts 1,000,000 frames back to back; CPU of both threads per frame, against A
//         delivering the same batches to itself inline (a group on the connection's loop)
//   idle  A posts one batch every 200 us, so B sleeps between batches: CPU of both threads
//         per batch (B's wake included) beyond the same loop with no post (A's own sleep and
//         wake), and post-to-run latency, 2,000 batches
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string_view>
#include <thread>
#include <vector>

namespace {
struct frame {
	std::uint64_t group;
	std::uint32_t size;
	char body[52];
};
using batch = std::vector<frame>;

std::uint64_t thread_cpu_ns() {
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return std::uint64_t(t.tv_sec) * 1'000'000'000 + std::uint64_t(t.tv_nsec);
}
std::uint64_t steady_ns() {
	return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
volatile std::uint64_t sink = 0;
void deliver(const batch& b) {
	std::uint64_t s = 0;
	for (const auto& f : b) { s += f.group + f.size + std::uint8_t(f.body[0]); }
	sink = sink + s;
}
batch make(std::size_t k, std::uint64_t first) {
	batch b(k);
	for (std::size_t i = 0; i < k; ++i) { b[i] = frame{first + i, 64, {char(i)}}; }
	return b;
}

// A loop with its own thread; cpu() is its thread's CPU time, read on that thread.
struct loop {
	asio::io_context io{1};
	asio::executor_work_guard<asio::io_context::executor_type> work = asio::make_work_guard(io);
	std::thread thread{[this] { io.run(); }};
	std::uint64_t cpu() {
		std::atomic<std::uint64_t> out{0};
		asio::post(io, [&] { out = thread_cpu_ns() + 1; });
		while (!out) { std::this_thread::yield(); }
		return out - 1;
	}
	~loop() {
		work.reset();
		thread.join();
	}
};

void busy(std::size_t k) {
	constexpr std::size_t frames = 1'000'000;
	const std::size_t batches = frames / k;
	loop a, b;
	std::atomic<std::size_t> done{0};
	// Inline: A builds and delivers each batch itself.
	std::atomic<bool> inline_done{false};
	std::uint64_t inline_cpu = 0;
	asio::post(a.io, [&] {
		const auto start = thread_cpu_ns();
		for (std::size_t i = 0; i < batches; ++i) { deliver(make(k, i * k)); }
		inline_cpu = thread_cpu_ns() - start;
		inline_done = true;
	});
	while (!inline_done) { std::this_thread::yield(); }
	// Hopped: A builds each batch and posts it to B, which delivers it.
	const auto a0 = a.cpu(), b0 = b.cpu();
	const auto wall0 = steady_ns();
	asio::post(a.io, [&] {
		for (std::size_t i = 0; i < batches; ++i) {
			asio::post(b.io, [&, x = make(k, i * k)] {
				deliver(x);
				done.fetch_add(1, std::memory_order_release);
			});
		}
	});
	while (done.load(std::memory_order_acquire) < batches) { std::this_thread::yield(); }
	const auto wall = steady_ns() - wall0;
	const auto a1 = a.cpu(), b1 = b.cpu();
	const double hop = double((a1 - a0) + (b1 - b0)) / frames, same = double(inline_cpu) / frames;
	std::printf("busy k=%zu inline_ns_per_frame=%.1f hop_ns_per_frame=%.1f (a=%.1f b=%.1f) added_ns_per_frame=%.1f wall_ns_per_frame=%.1f\n", k, same,
		hop, double(a1 - a0) / frames, double(b1 - b0) / frames, hop - same, double(wall) / frames);
}

struct idle_cost {
	double a, b;
};
idle_cost idle(std::size_t k, idle_cost base = {0, 0}) {
	constexpr std::size_t batches = 2'000;
	loop a, b;
	std::atomic<std::size_t> done{0};
	std::vector<std::uint64_t> latency(batches);
	const auto a0 = a.cpu(), b0 = b.cpu();
	asio::post(a.io, [&] {
		for (std::size_t i = 0; i < batches; ++i) {
			const auto posted = steady_ns();
			if (!k) {
				done.fetch_add(1, std::memory_order_release);
			} else asio::post(b.io, [&, i, posted, x = make(k, i * k)] {
				latency[i] = steady_ns() - posted;
				deliver(x);
				done.fetch_add(1, std::memory_order_release);
			});
			std::this_thread::sleep_for(std::chrono::microseconds(200));  // B sleeps in its reactor between batches
		}
	});
	while (done.load(std::memory_order_acquire) < batches) { std::this_thread::yield(); }
	const auto a1 = a.cpu(), b1 = b.cpu();
	const idle_cost cost{double(a1 - a0) / batches, double(b1 - b0) / batches};
	if (!k) {
		std::printf("idle k=0 (no post) a_cpu_ns_per_batch=%.0f b_cpu_ns_per_batch=%.0f\n", cost.a, cost.b);
		return cost;
	}
	std::sort(latency.begin(), latency.end());
	std::printf("idle k=%zu added_cpu_ns_per_batch=%.0f (a=%.0f b=%.0f) latency_us p50=%.1f p99=%.1f\n", k, cost.a - base.a + cost.b - base.b,
		cost.a - base.a, cost.b - base.b, double(latency[batches / 2]) / 1000, double(latency[batches * 99 / 100]) / 1000);
	return cost;
}
}  // namespace

int main(int argc, char** argv) {
	const std::string_view only = argc > 1 ? argv[1] : "all";
	for (std::size_t k : {1, 16, 64}) {
		if (only == "all" || only == "busy") {
			for (int run = 0; run < 3; ++run) { busy(k); }
		}
	}
	if (only == "all" || only == "idle") {
		const auto base = idle(0);
		for (std::size_t k : {1, 16}) { idle(k, base); }
	}
	return 0;
}
