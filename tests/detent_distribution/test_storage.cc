#include "detent_distribution/buffer.hh"
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
		{
			auto account = std::make_shared<StorageLedger>(1024 * 1024);
			StorageAllocator<int> allocator(account, allocate, release);
			auto raw = allocator.allocate(2);
			check(account->used() > 2 * sizeof(int));
			const auto attempts = allocations;
			rejects<std::bad_array_new_length>(
				[&] { allocator.allocate(std::numeric_limits<std::size_t>::max()); });
			check(allocations == attempts);
			allocator.deallocate(raw, 2);
			check(account->used() == 0);
			auto value = std::allocate_shared<int>(allocator, 17);
			std::weak_ptr<int> weak = value;
			const auto control_capacity = account->used();
			check(control_capacity > sizeof(int) && *value == 17);
			value.reset();
			check(weak.expired() && account->used() == control_capacity);
			weak.reset();
			check(account->used() == 0);
			struct FailingConstructor {
				FailingConstructor() { throw std::runtime_error("constructor failure"); }
			};
			rejects<std::runtime_error>([&] {
				std::allocate_shared<FailingConstructor>(
					StorageAllocator<FailingConstructor>(account, allocate, release));
			});
			check(account->used() == 0);
			rejects<std::runtime_error>(
				[&] { make_storage_owner<FailingConstructor>(account, allocate, release); });
			check(account->used() == 0);
			{
				auto unique = make_storage_owner<int>(account, allocate, release, 42);
				check(*unique == 42 && account->used() > sizeof(int));
				auto moved = std::move(unique);
				check(!unique && *moved == 42);
			}
			check(account->used() == 0);
			fail = true;
			rejects<std::bad_alloc>([&] { std::allocate_shared<int>(allocator); });
			rejects<std::bad_alloc>([&] { make_storage_owner<int>(account, allocate, release); });
			fail = false;
			check(account->used() == 0);
			{
				std::vector<int, StorageAllocator<int>> values(allocator);
				for (int i = 0; i < 1000; ++i)
					values.push_back(i);
				check(account->used() > values.capacity() * sizeof(int));
				check(account->peak() > account->used()); // replacement overlaps old capacity
			}
			check(account->used() == 0);
			auto empty = make_owned_buffer(account, 0);
			std::weak_ptr<OwnedBuffer> weak_buffer = empty;
			check(empty->storage_capacity() == 0 && account->used() >= sizeof(OwnedBuffer));
			empty.reset();
			check(weak_buffer.expired() && account->used() > 0);
			weak_buffer.reset();
			check(account->used() == 0);
			auto zero = std::make_shared<StorageLedger>(0);
			const auto before = allocations;
			rejects<StoragePressure>(
				[&] { std::allocate_shared<int>(StorageAllocator<int>(zero, allocate, release)); });
			rejects<StoragePressure>([&] { make_storage_owner<int>(zero, allocate, release); });
			check(allocations == before && zero->used() == 0);
		}
		{
			auto root = std::make_shared<StorageLedger>(1000);
			auto cache = std::make_shared<StorageLedger>(root, 400);
			auto publication = std::make_shared<StorageLedger>(root, 600);
			auto retained = cache->admit(400);
			rejects<StoragePressure>([&] { cache->admit(1); });
			auto publish = publication->admit(600);
			check(root->used() == 1000);
			auto moved = std::move(retained);
			check(retained.bytes() == 0 && moved.bytes() == 400);
			moved.reset();
			check(cache->used() == 0 && root->used() == 600);
			auto competing = std::make_shared<StorageLedger>(root, 1000);
			rejects<StoragePressure>([&] { competing->admit(401); });
			check(competing->used() == 0 && competing->peak() == 0 && root->used() == 600);
			publish.reset();
			check(root->used() == 0 && root->peak() == 1000);
			rejects<std::invalid_argument>([&] { StorageLedger invalid(root, 1001); });
			rejects<std::invalid_argument>([&] { StorageLedger invalid({}, 0); });
			auto deepest = root;
			for (std::size_t depth = 1; depth < StorageLedger::maximum_depth; ++depth)
				deepest = std::make_shared<StorageLedger>(deepest, 1000);
			rejects<std::invalid_argument>([&] { StorageLedger invalid(deepest, 1000); });
			auto ticket = deepest->admit(37);
			check(root->used() == 37);
			ticket.reset();
			check(root->used() == 0);
			auto value = std::allocate_shared<int>(StorageAllocator<int>(cache), 7);
			std::weak_ptr<int> weak = value;
			const auto charged = cache->used();
			value.reset();
			check(weak.expired() && charged > 0 && root->used() == charged);
			weak.reset();
			check(root->used() == 0 && cache->used() == 0);
			std::vector<std::jthread> workers;
			for (unsigned i = 0; i < 8; ++i) {
				workers.emplace_back([&, i] {
					auto leaf = i % 2 ? cache : publication;
					for (unsigned turn = 0; turn < 10000; ++turn) {
						try {
							auto claim = leaf->admit(100);
							check(root->used() <= root->limit() && leaf->used() <= leaf->limit());
							std::this_thread::yield();
						} catch (const StoragePressure &) {
						}
					}
				});
			}
			workers.clear();
			check(root->used() == 0 && cache->used() == 0 && publication->used() == 0);
		}
		allocations = frees = 0;
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
			auto account = std::make_shared<StorageLedger>(1000 + PagedStorage::page_overhead());
			auto buffer = std::make_shared<OwnedBuffer>(account, 1000);
			check(account->used() == 1000 + PagedStorage::page_overhead() && buffer->empty());
			buffer->resize(10);
			check(buffer->view() == std::string(10, '\0'));
			buffer->mutable_bytes()[0] = 'x';
			buffer->resize(1000);
			check(buffer->data()[0] == 'x' && buffer->data()[999] == '\0');
			rejects<StoragePressure>([&] { OwnedBuffer denied(account, 1); });
			rejects<std::logic_error>([&] { buffer->resize(999); });
			buffer->seal();
			rejects<std::logic_error>([&] { (void)buffer->mutable_bytes(); });
			std::shared_ptr<const void> original_owner = buffer;
			buffer.reset();
			check(account->used() == 1000 + PagedStorage::page_overhead());
			original_owner.reset();
			check(account->used() == 0);
			OwnedBuffer empty(account, 0);
			empty.seal();
			check(empty.view().empty() && account->used() == 0);
		}
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
