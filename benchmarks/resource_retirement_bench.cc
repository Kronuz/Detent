#include "detent_distribution/retirement.hh"
namespace resource = detent::distribution;
using Account = detent::distribution::StorageLedger;
#include <chrono>
#include <cstdio>
#include <ctime>
#include <vector>

struct Payload : resource::RetirementNode {
	static inline std::atomic<std::size_t> destroyed{0};
	~Payload() { destroyed.fetch_add(1, std::memory_order_relaxed); }
};

int main() {
	constexpr std::size_t count = 100000;
	auto baseline = std::make_shared<Account>(1024 * 1024);
	auto account = std::make_shared<Account>(64 * 1024 * 1024);
	bool failed = false;
	{
		resource::RetirementRuntime runtime(baseline);
		auto domain = runtime.domain(account, count + 1);
		std::vector<resource::RetirementHandle<Payload>> owners;
		owners.reserve(count);
		const auto start = std::chrono::steady_clock::now();
		const auto cpu_start = std::clock();
		for (std::size_t i = 0; i < count; ++i)
			owners.push_back(domain.make<Payload>());
		const auto created = std::chrono::steady_clock::now();
		owners.clear();
		const auto released = std::chrono::steady_clock::now();
		domain.close().wait();
		const auto drained = std::chrono::steady_clock::now();
		const double cpu = double(std::clock() - cpu_start) / CLOCKS_PER_SEC;
		const auto seconds = [](auto first, auto last) {
			return std::chrono::duration<double>(last - first).count();
		};
		std::printf("owners=%zu destroyed=%zu creation_seconds=%.9f release_seconds=%.9f "
					"drain_seconds=%.9f cpu_seconds=%.9f fixed_heap_bytes=%zu peak=%zu "
					"configured_stack_bytes=%zu\n",
					count, Payload::destroyed.load(), seconds(start, created),
					seconds(created, released), seconds(released, drained), cpu,
					runtime.fixed_heap_capacity(), account->peak(),
					runtime.configured_stack_bytes());
		runtime.shutdown();
		failed = Payload::destroyed.load() != count || domain.residents() != 0;
	}
	std::printf("payload_remaining=%zu controls_remaining=%zu\n", account->used(),
				baseline->used());
	return failed || account->used() || baseline->used();
}
