#include "detent_distribution/rows.hh"
#include "detent_distribution/storage.hh"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace detent::distribution;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("storage invariant failed");
}
std::size_t allocations = 0, frees = 0;
bool fail = false;
std::size_t until_failure = std::numeric_limits<std::size_t>::max();
void *allocate(std::size_t bytes) {
	++allocations;
	if (until_failure != std::numeric_limits<std::size_t>::max()) {
		if (!until_failure)
			return nullptr;
		--until_failure;
	}
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
			StorageLedger concurrent(64);
			std::vector<std::jthread> workers;
			for (unsigned i = 0; i < 8; ++i) {
				workers.emplace_back([&] {
					for (unsigned turn = 0; turn < 10000; ++turn) {
						try {
							auto ticket = concurrent.admit(16);
							check(concurrent.used() <= 64);
							std::this_thread::yield();
						} catch (const std::length_error &) {
						}
					}
				});
			}
			workers.clear();
			check(concurrent.used() == 0 && concurrent.peak() <= concurrent.limit());
		}
		{
			StorageLedger row_ledger(4 * 1024 * 1024);
			PagedStorage arena(row_ledger, 4096, allocate, release);
			StableRows<std::uint64_t> rows(arena);
			const std::string original("stable\0text", 11);
			auto text = store_text(arena, original);
			for (std::uint64_t i = 0; i < 70000; ++i)
				rows.push_back(i * 17);
			const auto *first = &rows[0];
			check(text.value == original && rows[65536] == 65536 * 17 && rows.back() == 69999 * 17);
			std::uint64_t ordinal = 0;
			for (auto row : rows)
				check(row == ordinal++ * 17);
			check(ordinal == rows.size());
			const auto retained = row_ledger.used();
			rows.pop_back();
			check(row_ledger.used() == retained && &rows[0] == first);
			rows.push_back(42);
			check(rows.back() == 42);
			rejects<std::out_of_range>([&] { (void)rows[rows.size()]; });
			StableRows<std::uint64_t> moved(std::move(rows));
			check(rows.empty() && moved.size() == 70000 && &moved[0] == first);
		}
		// Fail at each metadata/block growth allocation. Admitted partial
		// directory pages remain charged until the arena is actually retired.
		for (std::size_t growth = 0; growth < 4; ++growth) {
			StorageLedger row_ledger(1024 * 1024);
			{
				// Tiny quotas choose the exact failure boundary without needing
				// unaccounted replacement metadata in the allocator callback.
				std::size_t capacity = 0;
				for (std::size_t i = 0; i < growth; ++i)
					capacity += (i < 3 ? 256 * sizeof(void *) : 128 * sizeof(std::uint64_t)) +
								PagedStorage::page_overhead();
				StorageLedger tiny(capacity);
				PagedStorage limited(tiny, 1, allocate, release);
				StableRows<std::uint64_t> limited_rows(limited);
				rejects<std::length_error>([&] { limited_rows.push_back(17); });
				check(limited_rows.empty() && tiny.used() == capacity);
				while (!limited.release_one()) {
				}
				check(tiny.used() == 0);
				PagedStorage arena(row_ledger, 1, allocate, release);
				StableRows<std::uint64_t> rows(arena);
				until_failure = growth;
				rejects<std::bad_alloc>([&] { rows.push_back(17); });
				until_failure = std::numeric_limits<std::size_t>::max();
				check(rows.empty() && row_ledger.used() == capacity);
				rows.push_back(17);
				check(rows.size() == 1 && rows[0] == 17);
			}
			check(row_ledger.used() == 0);
		}
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
