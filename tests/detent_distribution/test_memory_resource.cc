#include "detent_distribution/memory_resource.hh"
#include <array>
#include <cstring>
#include <iostream>
#include <thread>
namespace d = detent::distribution;
namespace {
struct Allocation {
	void *pointer = nullptr;
	std::size_t bytes = 0;
};
std::array<Allocation, 32> live;
std::size_t live_bytes = 0, attempts = 0;
bool fail = false, early_refund = false;
d::StorageLedger *observed = nullptr;
void check(bool value) {
	if (!value)
		throw std::runtime_error("ledger resource invariant failed");
}
void *allocate(std::size_t bytes) {
	++attempts;
	if (fail)
		return nullptr;
	auto memory = std::malloc(bytes);
	if (!memory)
		return nullptr;
	for (auto &item : live) {
		if (!item.pointer) {
			item = {memory, bytes};
			live_bytes += bytes;
			return memory;
		}
	}
	std::abort();
}
void release(void *pointer) {
	if (observed && observed->used() < live_bytes)
		early_refund = true;
	for (auto &item : live) {
		if (item.pointer == pointer) {
			live_bytes -= item.bytes;
			item = {};
			std::free(pointer);
			return;
		}
	}
	std::abort();
}
template <class Exception, class Action> void rejects(Action action) {
	try {
		action();
	} catch (const Exception &) {
		return;
	}
	throw std::runtime_error("expected resource rejection");
}
} // namespace
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		observed = account.get();
		std::size_t capacity = 0;
		{
			auto resource = d::make_storage_resource(account, allocate, release);
			const auto baseline = account->used();
			check(baseline != 0 && baseline == live_bytes);
			for (auto alignment : {1u, 2u, 8u, 16u, 64u, 4096u}) {
				auto memory = resource->allocate(37, alignment);
				check(reinterpret_cast<std::uintptr_t>(memory) % alignment == 0);
				std::memset(memory, 'x', 37);
				check(account->used() == live_bytes && live_bytes > baseline + 37);
				resource->deallocate(memory, 37, alignment);
				check(account->used() == baseline && account->used() == live_bytes);
			}
			const auto before = attempts;
			rejects<std::bad_alloc>(
				[&] { (void)resource->allocate(std::numeric_limits<std::size_t>::max(), 64); });
			check(attempts == before && account->used() == baseline);
			fail = true;
			rejects<std::bad_alloc>([&] { (void)resource->allocate(64, 64); });
			fail = false;
			check(account->used() == baseline && live_bytes == baseline);
			auto memory = resource->allocate(64, 64);
			capacity = account->used();
			std::jthread releaser([held = resource, memory] { held->deallocate(memory, 64, 64); });
			releaser.join();
			check(account->used() == baseline && live_bytes == baseline);
			auto empty = resource->allocate(0, 64);
			check(account->used() == live_bytes && live_bytes > baseline);
			resource->deallocate(empty, 0, 64);
			check(account->used() == baseline);
		}
		check(account->used() == 0 && live_bytes == 0 && !early_refund);
		{
			auto tight = std::make_shared<d::StorageLedger>(capacity - 1);
			observed = tight.get();
			auto resource = d::make_storage_resource(tight, allocate, release);
			const auto before = attempts;
			const auto baseline = tight->used();
			rejects<d::StoragePressure>([&] { (void)resource->allocate(64, 64); });
			check(attempts == before && tight->used() == baseline && baseline == live_bytes);
		}
		check(live_bytes == 0 && !early_refund);
		observed = account.get();
		std::weak_ptr<std::pmr::memory_resource> weak;
		{
			auto resource = d::make_storage_resource(account, allocate, release);
			weak = resource;
		}
		check(weak.expired() && account->used() == live_bytes && live_bytes > 0);
		weak.reset();
		check(account->used() == 0 && live_bytes == 0 && !early_refund);
		auto zero = std::make_shared<d::StorageLedger>(0);
		const auto before = attempts;
		rejects<d::StoragePressure>([&] { d::make_storage_resource(zero, allocate, release); });
		check(attempts == before && zero->used() == 0);
		std::cout << "physical allocation, alignment, weak lifetime and quota conservation passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
