#include "detent_distribution/memory_resource.hh"
#include "detent_local/inventory.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <source_location>
#include <thread>
#include <unistd.h>
namespace l = detent::local;
namespace c = l::inventory_completion;
namespace {
thread_local bool watching = false;
thread_local std::size_t allocations = 0;
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("inventory invariant failed at line " + std::to_string(where.line()));
}
struct Lock final : c::OwnerLock {};
struct FakeIO;
struct Cursor final : c::DirectoryCursor {
	FakeIO &owner;
	std::uint64_t at = 0;
	bool closed = false;
	explicit Cursor(FakeIO &io) : owner(io) {}
	std::optional<std::string> next() override { throw std::logic_error("legacy scan used"); }
	std::optional<std::size_t> next_into(std::span<char> bytes) override;
	void close() override;
};
struct FakeIO final : c::IO {
	std::uint64_t entries = 0, inspections = 0, closes = 0, length = 3;
	bool missing = false, directory = false, close_error = false;
	c::ManagedCapabilities managed_capabilities() const noexcept override {
		return {.caller_scan = true, .entry_inspection = true, .cursor_close = true};
	}
	std::unique_ptr<c::OwnerLock> acquire_owner(bool) override { return std::make_unique<Lock>(); }
	std::unique_ptr<c::File> open_existing(std::string_view) override {
		throw std::logic_error("unexpected open");
	}
	std::unique_ptr<c::File> create_exclusive(std::string_view) override {
		throw std::logic_error("unexpected create");
	}
	void replace(std::string_view, std::string_view) override { throw std::logic_error("unexpected rename"); }
	void remove(std::string_view) override { throw std::logic_error("unexpected unlink"); }
	void sync_directory() override { throw std::logic_error("unexpected barrier"); }
	std::unique_ptr<c::DirectoryCursor> scan_directory() override { return std::make_unique<Cursor>(*this); }
	std::optional<c::EntryInspection> inspect_entry(std::string_view) override {
		++inspections;
		if (missing)
			return {};
		return c::EntryInspection{{directory ? c::EntryKind::Directory : c::EntryKind::Regular, length, {}},
								  1,
								  inspections,
								  42,
								  1,
								  0600};
	}
};
std::optional<std::size_t> Cursor::next_into(std::span<char> bytes) {
	check(!closed);
	if (at == owner.entries)
		return {};
	++at;
	const auto count =
		std::snprintf(bytes.data(), bytes.size(), "foreign-%llu", static_cast<unsigned long long>(at));
	check(count > 0 && std::size_t(count) < bytes.size());
	return std::size_t(count);
}
void Cursor::close() {
	check(!closed);
	closed = true;
	++owner.closes;
	if (owner.close_error)
		throw std::runtime_error("secondary close failure");
}
auto session(const std::shared_ptr<c::IO> &backend) {
	return l::make_storage_session({}, backend, std::make_shared<int>(1), true);
}
void step(l::InventoryOperation &job) {
	job.submitted();
	auto result = kronuz::journal::detail::execute_primitive(job.io(), job.request());
	check(job.complete(std::move(result)));
}
void finish(l::InventoryOperation &job) {
	while (!job.done())
		step(job);
}
void incomplete(l::InventoryOperation &job) {
	bool rejected = false;
	try {
		(void)job.report();
	} catch (const std::logic_error &) {
		rejected = true;
	}
	check(rejected);
}
} // namespace
void *operator new(std::size_t bytes) {
	if (watching)
		++allocations;
	if (auto pointer = std::malloc(bytes ? bytes : 1))
		return pointer;
	throw std::bad_alloc();
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
int main() {
	try {
		{
			auto backend = std::make_shared<FakeIO>();
			bool rejected = false;
			try {
				(void)l::make_inventory_operation(session(backend), UINT64_MAX, 42);
			} catch (const std::length_error &) {
				rejected = true;
			}
			check(rejected && backend->inspections == 0 && backend->closes == 0);
		}
		for (auto count : {std::uint64_t(1), std::uint64_t(100000)}) {
			auto backend = std::make_shared<FakeIO>();
			backend->entries = count;
			auto job = l::make_inventory_operation(session(backend), count, 42);
			allocations = 0;
			watching = true;
			finish(*job);
			watching = false;
			check(job->status() == l::InventoryStatus::Ready && allocations == 1);
			check(job->report().usage == l::DiskUsage{count * 3, count, 0});
			check(backend->inspections == count && backend->closes == 1);
			check(job->report().sample_count == std::min<std::uint64_t>(count, 8));
			check(job->report().quarantined == count);
			l::DiskLedger ledger({0, 0, 0});
			ledger.initialize(job->report().usage);
			check(ledger.stats().used == job->report().usage);
		}
		std::size_t fixed_capacity = 0;
		for (auto count : {std::uint64_t(1), std::uint64_t(100000)}) {
			auto account = std::make_shared<detent::distribution::StorageLedger>(16384);
			auto resource = detent::distribution::make_storage_resource(account);
			c::AllocationContext context(resource);
			auto backend = std::make_shared<FakeIO>();
			backend->entries = count;
			auto owner = l::make_storage_session(context, backend, std::make_shared<int>(1), true);
			const auto baseline = account->used();
			auto job = l::make_inventory_operation(owner, count, 42);
			const auto capacity = account->used() - baseline;
			if (!fixed_capacity)
				fixed_capacity = capacity;
			check(capacity == fixed_capacity && capacity > sizeof(l::InventoryOperation));
			std::weak_ptr<l::InventoryOperation> weak = job;
			auto pressure = account->admit(account->limit() - account->used());
			finish(*job);
			check(account->used() == account->limit());
			pressure.reset();
			job.reset();
			check(weak.expired() && account->used() > baseline);
			weak.reset();
			check(account->used() == baseline);
		}
		std::cout << "inventory admitted controls=" << fixed_capacity << " bytes for 1 and 100000 entries\n";
		for (unsigned failure = 0; failure < 5; ++failure) {
			auto backend = std::make_shared<FakeIO>();
			backend->entries = 2;
			backend->missing = failure == 0;
			backend->directory = failure == 1;
			backend->length = failure == 2 ? UINT64_MAX : 3;
			backend->close_error = failure == 4;
			auto job = l::make_inventory_operation(session(backend), failure == 3 ? 1 : 2, 42);
			finish(*job);
			check(job->status() == l::InventoryStatus::Failed && backend->closes == 1);
			incomplete(*job);
		}
		{
			auto backend = std::make_shared<FakeIO>();
			backend->entries = 2;
			backend->missing = true;
			backend->close_error = true;
			auto job = l::make_inventory_operation(session(backend), 2, 42);
			finish(*job);
			try {
				std::rethrow_exception(job->error());
			} catch (const std::runtime_error &error) {
				check(std::string_view(error.what()) == "entry disappeared during quiescent inventory");
			}
		}
		for (unsigned phase = 0; phase < 4; ++phase) {
			auto backend = std::make_shared<FakeIO>();
			backend->entries = 1;
			auto job = l::make_inventory_operation(session(backend), 1, 42);
			for (unsigned at = 0; at < phase; ++at)
				step(*job);
			job->submitted();
			auto result = kronuz::journal::detail::execute_primitive(job->io(), job->request());
			c::Result stale;
			stale.token = result.token;
			++stale.token.step;
			check(!job->complete(std::move(stale)) && job->in_flight());
			job->cancel();
			check(job->complete(std::move(result)));
			finish(*job);
			check(job->status() == l::InventoryStatus::Cancelled && backend->closes == 1);
			incomplete(*job);
		}
		{
			auto backend = std::make_shared<FakeIO>();
			auto job = l::make_inventory_operation(session(backend), 0, 42);
			job->submitted();
			auto result = kronuz::journal::detail::execute_primitive(job->io(), job->request());
			job->cancel();
			check(job->complete(std::move(result)));
			check(!job->done() && job->request().kind == c::Kind::CloseCursor);
			job->cancel();
			check(!job->done());
			finish(*job);
			check(job->status() == l::InventoryStatus::Cancelled && backend->closes == 1);
		}
		const auto root =
			std::filesystem::current_path() / ".scratch" / ("inventory-test-" + std::to_string(::getpid()));
		std::filesystem::create_directories(root);
		std::filesystem::permissions(root, std::filesystem::perms::owner_all);
		std::ofstream(root / "foreign") << "abc";
		std::ofstream(root / "generation-00000000000000000000000000000000.lease") << "bad";
		std::ofstream(root / "generation-00000000000000000000000000000000.image") << "image";
		for (const auto name : {"foreign", "generation-00000000000000000000000000000000.lease",
								"generation-00000000000000000000000000000000.image"})
			std::filesystem::permissions(root / name, std::filesystem::perms::owner_read |
														  std::filesystem::perms::owner_write);
		std::filesystem::create_symlink("missing", root / "symlink");
		std::filesystem::create_hard_link(root / "foreign", root / "hardlink");
		{
			auto backend = std::make_shared<c::PosixIO>(root);
			auto job = l::make_inventory_operation(session(backend), 6, ::geteuid());
			finish(*job);
			check(job->status() == l::InventoryStatus::Ready);
			check(job->report().usage == l::DiskUsage{21, 6, 1});
			check(job->report().canonical_payloads == 1 && job->report().quarantined == 3);
		}
		for (const auto name :
			 {"segment-0123456789abcdef0123456789abcdef.owner",
			  "segment-0123456789abcdef0123456789abcdef.pack",
			  "segment-00000000000000000000000000000000.pack",
			  "segment-FEDCBA98765432100123456789abcdef.pack", "segment-0123456789abcdef0123456789abcde.pack",
			  "segment-0123456789abcdef0123456789abcdef.pack.extra"}) {
			std::ofstream(root / name) << "x";
			std::filesystem::permissions(root / name, std::filesystem::perms::owner_read |
														  std::filesystem::perms::owner_write);
		}
		{
			auto backend = std::make_shared<c::PosixIO>(root);
			auto owner = l::make_storage_session({}, backend, std::make_shared<int>(1), false);
			auto job = l::make_inventory_operation(owner, 12, ::geteuid());
			finish(*job);
			check(job->status() == l::InventoryStatus::Ready);
			check(job->report().usage == l::DiskUsage{27, 12, 1});
			check(job->report().canonical_payloads == 3 && job->report().quarantined == 6);
		}
		{
			auto backend = std::make_shared<c::PosixIO>(root);
			auto owner = l::make_storage_session({}, backend, std::make_shared<int>(1), false);
			auto job = l::make_inventory_operation(owner, 6, ::geteuid());
			std::weak_ptr<l::InventoryOperation> weak = job;
			c::NativeQueue queue;
			check(queue.submit(job));
			job->cancel();
			job.reset();
			owner.reset();
			backend.reset();
			auto poll = [&] {
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				for (;;) {
					auto result = queue.poll();
					if (result)
						return result;
					check(std::chrono::steady_clock::now() < deadline);
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			};
			auto held = poll();
			check(!weak.expired());
			job = std::static_pointer_cast<l::InventoryOperation>(held->operation);
			check(job->complete(std::move(held->completion)) && !job->done());
			check(job->request().kind == c::Kind::CloseCursor);
			check(queue.submit(job));
			held.reset();
			job.reset();
			held = poll();
			job = std::static_pointer_cast<l::InventoryOperation>(held->operation);
			check(!job->done() && job->in_flight());
			check(job->complete(std::move(held->completion)) &&
				  job->status() == l::InventoryStatus::Cancelled);
			incomplete(*job);
			held.reset();
			job.reset();
			check(weak.expired());
		}

		std::filesystem::remove_all(root);
		std::cout << "bounded quiescent inventory and authentic cursor settlement passed\n";
	} catch (const std::exception &error) {
		watching = false;
		std::cerr << error.what() << '\n';
		return 1;
	}
}
