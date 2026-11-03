// T13 (docs/design/http-coroutines.md): a coordinator whose loop is stopped while its tasks are suspended
// leaves no live task frame and no orphaned waiter after D15's phases.
#include "detent/owner_loop.hh"
#include "detent_delivery/coordinator.hh"
#include "detent_distribution/memory_resource.hh"
#include "task_support.hh"
#include <atomic>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>
namespace d = detent::distribution;
namespace t = detent::delivery;
namespace l = detent::local;
using namespace std::chrono_literals;
static void require(bool ok, const char *what) {
	if (!ok)
		throw std::runtime_error(what);
}
// Phases 2-3 are what a forced stop needs; `skip` removes one for the mutants (ledger).
enum class Skip { None, Stop, Close, Destroy };
static Skip skipped() {
	const char *name = std::getenv("DETENT_TEARDOWN_SKIP");
	if (!name)
		return Skip::None;
	const std::string_view skip = name;
	return skip == "stop" ? Skip::Stop : skip == "close" ? Skip::Close : skip == "destroy" ? Skip::Destroy : Skip::None;
}
// Runs on the owner: waits for the coordinator's first failed reconnect and its quiet period, then flags.
static task::task<void> quiesce(task::asio_scheduler &scheduler, std::shared_ptr<t::Coordinator> coordinator,
								std::atomic<bool> &ready) {
	asio::steady_timer timer(scheduler.executor());
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (!coordinator->status().failures) {
		require(std::chrono::steady_clock::now() < deadline, "the coordinator never failed a connect");
		timer.expires_after(1ms);
		co_await detent::transport::await_timer(timer);
	}
	timer.expires_after(100ms);
	co_await detent::transport::await_timer(timer);
	ready.store(true);
}
int main() {
	try {
		const auto frames = task::detail::live_frames();
		const auto root =
			std::filesystem::current_path() / ".scratch" / ("coordinator-teardown-" + std::to_string(::getpid()));
		std::filesystem::create_directories(root);
		std::filesystem::permissions(root, std::filesystem::perms::owner_all);
		auto account = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 64);
		asio::io_context sockets;
		asio::ip::tcp::socket unavailable(sockets);
		unavailable.open(asio::ip::tcp::v4());
		unavailable.bind({asio::ip::address_v4::loopback(), 0});
		asio::thread_pool worker(1);
		l::DiskStoreOptions disk;
		disk.mode = l::StoreOpenMode::FreshEmpty;
		disk.limits = {1024 * 1024, 100, 10};
		auto store = l::make_disk_store(domain, root, account, disk);
		auto queue = kronuz::io::completion::make_native_queue(
			kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
		{
			detent::OwnerLoop owner;
			std::shared_ptr<t::Coordinator> coordinator;
			std::shared_ptr<t::NativeDriver> driver;
			driver = std::allocate_shared<t::NativeDriver>(d::StorageAllocator<t::NativeDriver>(account),
														   owner.scheduler(), queue);
			t::CoordinatorOptions options;
			options.source.scheduler = &owner.scheduler();
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
			coordinator = t::make_coordinator(options, store, driver, domain, worker);
			std::atomic<bool> ready{false};
			asio::post(owner.io(), [&] {
				task::spawn(task::scheduler_ref::of(owner.scheduler()), coordinator->run());
				task::spawn(task::scheduler_ref::of(owner.scheduler()), quiesce(owner.scheduler(), coordinator, ready));
			});
			for (auto limit = std::chrono::steady_clock::now() + 10s; !ready.load(); std::this_thread::sleep_for(1ms))
				require(std::chrono::steady_clock::now() < limit, "the coordinator did not reach its reconnect wait");
			// Forced stop: the coordinator is neither closed nor drained, its tasks are suspended.
			const auto skip = skipped();
			if (skip != Skip::Stop)
				owner.stop();
			if (skip != Skip::Close)
				owner.close();
			if (skip != Skip::Destroy)
				owner.destroy_roots();
			// Phase 4: what holds timers on the owner's io_context goes before the io_context.
			worker.join();
			coordinator.reset();
			driver.reset();
			require(task::detail::live_frames() == frames,
					("live task frames after teardown: " + std::to_string(task::detail::live_frames() - frames)).c_str());
			require(task::detail::orphaned_waiters() == 0, "orphaned waiters after teardown");
		}
		queue.reset();
		store.reset();
		domain.close().wait();
		runtime.shutdown();
		std::filesystem::remove_all(root);
		require(task::detail::live_frames() == frames, "live task frames at exit");
		std::cout << "a forced stop leaves no live task frame\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
