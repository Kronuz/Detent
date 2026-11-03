#include "detent_delivery/coordinator.hh"
#include "task_support.hh"
#include "detent_distribution/memory_resource.hh"
#include <asio.hpp>
#include <filesystem>
#include <future>
#include <iostream>
#include <unistd.h>
namespace d = detent::distribution;
namespace t = detent::delivery;
namespace l = detent::local;
using namespace std::chrono_literals;
static void require(bool ok) {
	if (!ok)
		throw std::runtime_error("reconnect cancellation invariant");
}
task::task<void> observe(task::asio_scheduler &scheduler, std::shared_ptr<t::Coordinator> coordinator) {
	asio::steady_timer timer(scheduler.executor());
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (!coordinator->status().failures) {
		require(std::chrono::steady_clock::now() < deadline);
		timer.expires_after(1ms);
		co_await detent::transport::await_timer(timer);
	}
	timer.expires_after(100ms);
	co_await detent::transport::await_timer(timer);
	require(coordinator->status().failures == 1);
	const auto began = std::chrono::steady_clock::now();
	coordinator->close();
	while (!coordinator->drained()) {
		require(std::chrono::steady_clock::now() - began < 1s);
		timer.expires_after(1ms);
		co_await detent::transport::await_timer(timer);
	}
}
int main() {
	try {
		const auto root =
			std::filesystem::current_path() / ".scratch" / ("reconnect-cancel-" + std::to_string(::getpid()));
		std::filesystem::create_directories(root);
		std::filesystem::permissions(root, std::filesystem::perms::owner_all);
		auto account = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 64);
		asio::io_context io(1);
		task::asio_scheduler scheduler(io.get_executor());
		asio::thread_pool worker(1);
		// A bound socket without listen refuses connects without a port-allocation race.
		asio::ip::tcp::socket unavailable(io);
		unavailable.open(asio::ip::tcp::v4());
		unavailable.bind({asio::ip::address_v4::loopback(), 0});
		l::DiskStoreOptions disk;
		disk.mode = l::StoreOpenMode::FreshEmpty;
		disk.limits = {1024 * 1024, 100, 10};
		auto store = l::make_disk_store(domain, root, account, disk);
		auto queue = kronuz::io::completion::make_native_queue(
			kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
		auto driver = std::allocate_shared<t::NativeDriver>(d::StorageAllocator<t::NativeDriver>(account),
															scheduler, queue);
		t::CoordinatorOptions options;
		options.source.scheduler = &scheduler;
		options.directory = root;
		options.delivery_mode = t::DeliveryMode::Subscription;
		options.poll_interval = 30s;
		options.reconnect_maximum = 60s;
		options.reconnect_seed = 42;
		options.source.endpoint = unavailable.local_endpoint();
		options.source.hostname = "localhost";
		options.source.token = "0123456789abcdef0123456789abcdef";
		options.source.configured.cluster.fill('c');
		options.source.configured.group.fill('g');
		options.source.configured.restore_epoch.fill('e');
		options.source.configured.scope.fill('s');
		options.source.tls_context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
		auto coordinator = t::make_coordinator(options, store, driver, domain, worker);
		auto run = spawn_future(scheduler, coordinator->run());
		auto observed = spawn_future(scheduler, observe(scheduler, coordinator));
		io.run();
		run.get();
		observed.get();
		require(coordinator->drained() && store->drained());
		worker.join();
		coordinator.reset();
		driver.reset();
		queue.reset();
		store.reset();
		domain.close().wait();
		runtime.shutdown();
		std::filesystem::remove_all(root);
		std::cout << "close interrupts a 15–30 second reconnect wait and drains originals\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
