#include "detent_distribution/memory_resource.hh"
#include "detent_local/inventory.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <thread>
#include <unistd.h>
namespace l = detent::local;
namespace c = l::inventory_completion;
namespace d = detent::distribution;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("collector invariant failed at line " + std::to_string(where.line()));
}
template <class Action> void rejects(Action action) {
	bool rejected = false;
	try {
		action();
	} catch (const std::exception &) {
		rejected = true;
	}
	check(rejected);
}
class Resource final : public std::pmr::memory_resource {
  public:
	explicit Resource(std::shared_ptr<std::pmr::memory_resource> upstream) : upstream_(std::move(upstream)) {}
	std::size_t attempts = 0, calls = 0, live = 0;
	std::optional<std::size_t> fail_at;

  private:
	void *do_allocate(std::size_t bytes, std::size_t align) override {
		if (++attempts == fail_at)
			throw std::bad_alloc();
		auto pointer = upstream_->allocate(bytes, align);
		++calls;
		live += bytes;
		return pointer;
	}
	void do_deallocate(void *pointer, std::size_t bytes, std::size_t align) override {
		upstream_->deallocate(pointer, bytes, align);
		live -= bytes;
	}
	bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override {
		return this == &other;
	}
	std::shared_ptr<std::pmr::memory_resource> upstream_;
};
void drive(l::InventoryOperation &job) {
	while (!job.done()) {
		job.submitted();
		check(job.complete(kronuz::journal::detail::execute_primitive(job.io(), job.request())));
	}
}
} // namespace
int main() {
	try {
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("collector-test-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		std::ofstream(directory / "foreign") << "abc";
		std::filesystem::permissions(directory / "foreign", std::filesystem::perms::owner_read |
																std::filesystem::perms::owner_write);
		auto account = std::make_shared<d::StorageLedger>(32768);
		auto upstream = d::make_storage_resource(account);
		auto resource = std::make_shared<Resource>(upstream);
		c::AllocationContext context(resource);
		{
			auto backend = c::make_posix_io(context, directory);
			auto owner = l::make_storage_session(context, backend, std::make_shared<int>(1), true);
			const auto baseline = account->used(), live = resource->live;
			const auto before = resource->calls;
			auto probe = l::make_collector_controls(owner);
			const auto collector_calls = resource->calls - before;
			check(collector_calls == 11);
			probe.reset();
			check(account->used() == baseline && resource->live == live);
			for (std::size_t at = 1; at <= collector_calls; ++at) {
				resource->fail_at = resource->attempts + at;
				rejects([&] { l::make_collector_controls(owner); });
				resource->fail_at = {};
				check(account->used() == baseline && resource->live == live);
			}
			auto controls = l::make_collector_controls(owner);
			for (std::size_t slot = 0; slot < l::CollectorControls::batch_capacity; ++slot) {
				auto file = dynamic_cast<kronuz::journal::detail::PosixFile *>(&controls->lease(slot));
				check(file && file->native_handle() < 0);
			}
			rejects([&] { controls->lease(8); });
			const auto with_controls = account->used(), controls_live = resource->live;
			const auto before_inventory = resource->calls;
			auto inventory = l::make_inventory_operation(controls, 2, ::geteuid());
			check(resource->calls - before_inventory == 2 && controls->active());
			inventory->cancel();
			check(inventory->status() == l::InventoryStatus::Cancelled && !controls->active());
			inventory.reset();
			check(account->used() == with_controls && resource->live == controls_live);
			for (std::size_t at = 1; at <= 2; ++at) {
				resource->fail_at = resource->attempts + at;
				rejects([&] { l::make_inventory_operation(controls, 2, ::geteuid()); });
				resource->fail_at = {};
				check(account->used() == with_controls && resource->live == controls_live &&
					  !controls->active());
			}
			inventory = l::make_inventory_operation(controls, 2, ::geteuid());
			auto queue = c::make_native_queue(context);
			const auto calls = resource->calls, capacity = account->used();
			std::array<c::File *, 8> addresses;
			for (std::size_t slot = 0; slot < addresses.size(); ++slot)
				addresses[slot] = &controls->lease(slot);
			auto cursor_address = &controls->cursor();
			auto pressure = account->admit(account->limit() - account->used());
			c::Token old_scan;
			for (unsigned pass = 0; pass < 100; ++pass) {
				if (pass)
					inventory->restart(2, ::geteuid());
				check(controls->active());
				rejects([&] { controls->claim(l::CollectorUse::Reclaim); });
				if (!pass)
					old_scan = inventory->request().token;
				drive(*inventory);
				check(!controls->active() && inventory->status() == l::InventoryStatus::Ready);
				check(inventory->report().usage == l::DiskUsage{3, 2, 0});
				check(resource->calls == calls && account->used() == account->limit());
				check(&controls->cursor() == cursor_address);
				for (std::size_t slot = 0; slot < addresses.size(); ++slot)
					check(&controls->lease(slot) == addresses[slot]);
			}
			auto claim = controls->claim(l::CollectorUse::Reclaim);
			rejects([&] { inventory->restart(2, ::geteuid()); });
			check(inventory->status() == l::InventoryStatus::Ready && controls->active());
			auto stale_claim = claim;
			++stale_claim.serial;
			rejects([&] { controls->release(stale_claim); });
			check(controls->active());
			controls->release(claim);
			rejects([&] { controls->release(claim); });
			inventory->restart(2, ::geteuid());
			check(queue->submit(inventory));
			inventory->cancel();
			std::weak_ptr<l::InventoryOperation> weak = inventory;
			inventory.reset();
			auto poll = [&] {
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				for (;;) {
					auto result = queue->poll();
					if (result)
						return result;
					check(std::chrono::steady_clock::now() < deadline);
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			};
			auto held = poll();
			inventory = std::static_pointer_cast<l::InventoryOperation>(held->operation);
			c::Result replay;
			replay.token = old_scan;
			check(!inventory->complete(std::move(replay)) && inventory->in_flight());
			check(inventory->complete(std::move(held->completion)) && !inventory->done() &&
				  controls->active());
			check(inventory->request().kind == c::Kind::CloseCursor);
			check(queue->submit(inventory));
			held.reset();
			inventory.reset();
			held = poll();
			inventory = std::static_pointer_cast<l::InventoryOperation>(held->operation);
			check(controls->active() && !inventory->done());
			check(inventory->complete(std::move(held->completion)));
			check(!controls->active() && inventory->status() == l::InventoryStatus::Cancelled);
			held.reset();
			inventory->restart(1, ::geteuid());
			drive(*inventory);
			check(inventory->status() == l::InventoryStatus::Failed && !controls->active());
			inventory->restart(2, ::geteuid());
			drive(*inventory);
			check(inventory->status() == l::InventoryStatus::Ready);
			check(resource->calls == calls && account->used() == account->limit());
			pressure.reset();
			inventory.reset();
			check(weak.expired());
			weak.reset();
			queue.reset();
			controls.reset();
			check(account->used() == baseline && resource->live == live);
			std::cout << "collector/session/backend/queue/census=" << capacity
					  << " admitted bytes; 100 allocation-free real passes\n";
		}
		check(resource->live == 0);
		context = {};
		resource.reset();
		upstream.reset();
		check(account->used() == 0);
		std::filesystem::remove_all(directory);
		std::cout << "bootstrap allocation rollback, reusable saturated census and retained close settlement "
					 "passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
