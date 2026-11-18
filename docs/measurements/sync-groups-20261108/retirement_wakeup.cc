// Stress for a lost wakeup between RetirementDomain::close and the retirement worker.
// Each round opens a domain, releases residents whose cleanup takes about a microsecond each, waits a
// varying spin so that close() lands around the worker's last cleanup, closes the domain and waits
// for drainage. A drain that stays pending for 2 s with no resident left is one the worker never
// revisits: the probe prints it and exits 1. Exits 0 when every round drains.
#include "detent_distribution/retirement.hh"
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <vector>

namespace d = detent::distribution;
namespace {
void spin(std::chrono::nanoseconds duration) {
	const auto until = std::chrono::steady_clock::now() + duration;
	while (std::chrono::steady_clock::now() < until) {
	}
}
struct Owner final : d::RetirementNode {
	bool retire_step() noexcept override {
		spin(std::chrono::microseconds(1));
		return true;
	}
};
} // namespace

int main(int argc, char **argv) {
	const long rounds = argc > 1 ? std::strtol(argv[1], nullptr, 10) : 300'000;
	const auto stall = std::chrono::seconds(2);
	auto baseline = std::make_shared<d::StorageLedger>(std::size_t(1) << 20);
	auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 24);
	d::RetirementRuntime runtime(baseline, 1);
	const auto began = std::chrono::steady_clock::now();
	std::uint64_t seed = 0x9e3779b97f4a7c15;
	for (long round = 0; round < rounds; ++round) {
		seed = seed * 6364136223846793005 + 1442695040888963407;
		const auto owners_count = 1 + (seed >> 33) % 32;
		auto domain = runtime.domain(account, 64);
		{
			std::vector<d::RetirementHandle<Owner>> owners;
			for (std::uint64_t i = 0; i < owners_count; ++i)
				owners.push_back(domain.make<Owner>());
		}
		spin(std::chrono::nanoseconds((seed >> 13) % ((owners_count + 20) * 1000)));
		const auto drain = domain.close();
		const auto deadline = std::chrono::steady_clock::now() + stall;
		while (!drain.done() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		if (!drain.done()) {
			std::printf("round %ld: drain still pending after 2 s with %zu residents: lost wakeup\n", round,
						domain.residents());
			std::fflush(stdout);
			std::_Exit(1);
		}
		if (round % 100'000 == 0) {
			const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - began;
			std::fprintf(stderr, "progress: %ld rounds, %.1f s\n", round, elapsed.count());
		}
	}
	std::printf("%ld rounds drained\n", rounds);
}
