#include "detent/hashing.hh"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
std::atomic<std::uint64_t> allocations{0};
bool counting = false;
void *operator new(std::size_t n) {
	if (counting)
		++allocations;
	if (void *p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
int main() {
	std::string name = "production:percentage_rollout", hash(128, 'h');
	constexpr int n = 100000;
	counting = true;
	auto start = std::chrono::steady_clock::now();
	std::uint64_t sum = 0;
	for (int i = 0; i < n; ++i)
		sum += detent::stable_bucket(name, hash);
	auto elapsed = std::chrono::steady_clock::now() - start;
	counting = false;
	std::cout << "calls=" << n << " allocations=" << allocations
			  << " elapsed_us=" << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
			  << " oracle=" << sum << '\n';
}
