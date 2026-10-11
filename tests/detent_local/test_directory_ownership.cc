#include "detent_local/generation.hh"
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <new>
#include <unistd.h>
#include <utility>

namespace {
thread_local bool fail_next_allocation = false;
unsigned open_descriptors() {
	unsigned count = 0;
	for (int fd = 0; fd < 4096; ++fd)
		count += ::fcntl(fd, F_GETFD) >= 0;
	return count;
}
} // namespace
void *operator new(std::size_t bytes) {
	if (std::exchange(fail_next_allocation, false))
		throw std::bad_alloc();
	if (auto pointer = std::malloc(bytes ? bytes : 1))
		return pointer;
	throw std::bad_alloc();
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
	try {
		auto directory = std::filesystem::current_path() / ".scratch" /
						 ("directory-owner-test-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto account = std::make_shared<detent::distribution::StorageLedger>(1024 * 1024);
		detent::distribution::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 64);
		detent::local::ViewLimits limits;
		limits.retirement_domain = &domain;
		const auto before = open_descriptors();
		bool rejected = false;
		fail_next_allocation = true;
		try {
			detent::local::recover_generation(directory, limits);
		} catch (const std::bad_alloc &) {
			rejected = true;
		}
		if (!rejected || fail_next_allocation)
			throw std::runtime_error("directory recovery allocation failure was not injected");
		domain.close().wait();
		if (open_descriptors() != before)
			throw std::runtime_error("directory descriptor remained after failed recovery retirement");
		auto missing = runtime.domain(account, 64);
		limits.retirement_domain = &missing;
		rejected = false;
		try {
			detent::local::recover_generation(directory, limits);
		} catch (const std::runtime_error &) {
			rejected = true;
		}
		missing.close().wait();
		if (!rejected || open_descriptors() != before)
			throw std::runtime_error("directory descriptor leaked after missing selection");
		std::filesystem::remove_all(directory);
		std::cout << "directory ownership survives allocation and recovery failures\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
