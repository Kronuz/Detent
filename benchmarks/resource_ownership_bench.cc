#include "detent_distribution/memory_resource.hh"
using Account = detent::distribution::StorageLedger;
using Pages = detent::distribution::PagedStorage;
template <class T> using Charged = detent::distribution::StorageAllocator<T>;
using Resource = detent::distribution::LedgerMemoryResource;
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>
std::size_t upstream_bytes = 0, upstream_calls = 0;
void *upstream(std::size_t bytes) {
	upstream_bytes += bytes;
	++upstream_calls;
	return std::malloc(bytes);
}
void release(void *pointer) { std::free(pointer); }
template <class F> void measure(std::string_view name, std::size_t count, F operation) {
	const auto start = std::chrono::steady_clock::now();
	const auto cpu_start = std::clock();
	for (std::size_t i = 0; i < count; ++i)
		operation();
	const auto elapsed =
		std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	const double cpu = double(std::clock() - cpu_start) / CLOCKS_PER_SEC;
	std::cout << name << " operations=" << count << " seconds=" << elapsed << " cpu_seconds=" << cpu
			  << '\n';
}
#if defined(__APPLE__)
#include <malloc/malloc.h>
void account_heap() {
	constexpr std::size_t count = 4096;
	std::vector<std::shared_ptr<Account>> accounts(count);
	malloc_statistics_t before{}, live{}, released{};
	malloc_zone_statistics(nullptr, &before);
	for (auto &account : accounts) {
		account = std::make_shared<Account>(1024 * 1024);
		asm volatile("" : : "g"(account.get()) : "memory");
	}
	malloc_zone_statistics(nullptr, &live);
	accounts.clear();
	malloc_zone_statistics(nullptr, &released);
	std::cout << "account-heap accounts=" << count
			  << " live_blocks=" << live.blocks_in_use - before.blocks_in_use
			  << " live_bytes=" << live.size_in_use - before.size_in_use << " residual_blocks="
			  << static_cast<long long>(released.blocks_in_use) - static_cast<long long>(before.blocks_in_use)
			  << " residual_bytes="
			  << static_cast<long long>(released.size_in_use) - static_cast<long long>(before.size_in_use) << '\n';
}
#endif
int main() {
	constexpr std::size_t limit = 1024 * 1024;
	std::cout << "account_bytes=" << sizeof(Account) << " charge_bytes=" << sizeof(Account::Ticket)
			  << " page_header_bytes=" << Pages::page_overhead()
			  << " allocator_header_bytes=" << Charged<int>::allocation_overhead()
			  << " arena_bytes=" << sizeof(Pages) << '\n';
#if defined(__APPLE__)
	account_heap();
#endif
	std::size_t checksum = 0;
	measure("account-construction", 100000, [&] {
		Account account(limit);
		asm volatile("" : : "g"(&account) : "memory");
		checksum += account.limit();
	});
	auto root = std::make_shared<Account>(limit);
	measure("root-admission", 5000000, [&] {
		auto charge = root->admit(64);
		checksum += charge.bytes();
	});
	auto child = root;
	for (std::size_t i = 1; i < Account::maximum_depth; ++i)
		child = std::make_shared<Account>(child, limit);
	measure("depth-eight-admission", 1000000, [&] {
		auto charge = child->admit(64);
		checksum += charge.bytes();
	});
	measure("shared-control", 1000000, [&] {
		auto value = std::allocate_shared<int>(Charged<int>(root, upstream, release), 42);
		checksum += *value;
	});
	Resource resource(root, upstream, release);
	measure("overaligned-pmr", 100000, [&] {
		auto pointer = resource.allocate(64, 4096);
		resource.deallocate(pointer, 64, 4096);
	});
	measure("single-page-arena", 100000, [&] {
		Pages pages(*root, 1024, upstream, release);
		(void)pages.allocate(512);
	});
	std::cout << "checksum=" << checksum << " upstream_bytes=" << upstream_bytes
			  << " upstream_calls=" << upstream_calls << " used=" << root->used()
			  << " peak=" << root->peak() << '\n';
	return root->used() != 0;
}
