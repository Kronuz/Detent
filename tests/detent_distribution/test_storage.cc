#include "detent_distribution/storage.hh"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace detent::distribution;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("storage invariant failed");
}
std::size_t allocations = 0, frees = 0;
bool fail = false;
void *allocate(std::size_t bytes) {
	++allocations;
	return fail ? nullptr : std::malloc(bytes);
}
void release(void *pointer) {
	++frees;
	std::free(pointer);
}
template <class Exception, class F> void rejects(F action) {
	try {
		action();
	} catch (const Exception &) {
		return;
	}
	throw std::runtime_error("expected storage rejection");
}
} // namespace
int main() {
	try {
		constexpr auto page = 64 + PagedStorage::page_overhead();
		StorageLedger ledger(page * 2);
		{
			PagedStorage storage(ledger, 64, allocate, release);
			auto first = static_cast<char *>(storage.allocate(63, 1));
			std::memset(first, 'x', 63);
			check(ledger.used() == page);
			storage.allocate(1, 1);
			check(allocations == 1);
			auto second = storage.allocate(64);
			check(reinterpret_cast<std::uintptr_t>(second) % alignof(std::max_align_t) == 0);
			check(ledger.used() == page * 2 && ledger.peak() == page * 2);
			rejects<std::length_error>([&] { storage.allocate(1); });
			check(allocations == 2 && first[62] == 'x');
			PagedStorage moved(std::move(storage));
			check(storage.release_one() && ledger.used() == page * 2);
			check(!moved.release_one() && ledger.used() == page && frees == 1);
			rejects<std::logic_error>([&] { moved.allocate(1); });
			check(first[62] == 'x');
			check(moved.release_one() && ledger.used() == 0 && frees == 2);
			check(moved.release_one());
		}
		{
			PagedStorage storage(ledger, 64, allocate, release);
			fail = true;
			rejects<std::bad_alloc>([&] { storage.allocate(8); });
			check(ledger.used() == 0);
			fail = false;
			storage.allocate(8);
			check(ledger.used() == page);
			rejects<std::invalid_argument>([&] { storage.allocate(1, 3); });
			rejects<std::length_error>([&] { storage.allocate(std::numeric_limits<std::size_t>::max()); });
			check(ledger.used() == page);
		}
		check(ledger.used() == 0);
		{
			auto ticket = ledger.admit(7);
			auto other = ledger.admit(9);
			ticket = std::move(other);
			check(ledger.used() == 9 && other.bytes() == 0);
		}
		check(ledger.used() == 0);
		std::cout << "storage admission invariants passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
