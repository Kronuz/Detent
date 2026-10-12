#include "coordinator_internal.hh"
#include "detent_distribution/memory_resource.hh"
#include <asio.hpp>
#include <csignal>
#include <filesystem>
#include <future>
#include <iostream>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace d = detent::distribution;
namespace t = detent::delivery;
namespace l = detent::local;
using namespace std::chrono_literals;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("worker lifetime invariant");
}
std::filesystem::path directory(pid_t pid) {
	return std::filesystem::current_path() / ".scratch" / ("delivery-worker-" + std::to_string(pid));
}
struct Gate {
	std::promise<void> promise;
	bool released = false;
	explicit Gate(asio::thread_pool &worker) {
		auto future = promise.get_future().share();
		asio::post(worker, [future] { future.wait(); });
	}
	void release() {
		if (!released) {
			released = true;
			promise.set_value();
		}
	}
	~Gate() { release(); }
};
asio::awaitable<void> observe(std::weak_ptr<t::Coordinator> weak, Gate &gate) {
	std::exception_ptr failure;
	try {
		asio::steady_timer timer(co_await asio::this_coro::executor);
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		for (;;) {
			auto coordinator = weak.lock();
			check(static_cast<bool>(coordinator));
			if (coordinator->status().worker_pending) {
				coordinator->close();
				coordinator.reset();
				timer.expires_after(10ms);
				co_await timer.async_wait(asio::use_awaitable);
				auto retained = weak.lock();
				check(retained && retained->status().worker_pending && !retained->drained());
				retained.reset();
				gate.release();
				co_return;
			}
			check(std::chrono::steady_clock::now() < deadline);
			coordinator.reset();
			timer.expires_after(1ms);
			co_await timer.async_wait(asio::use_awaitable);
		}
	} catch (...) {
		failure = std::current_exception();
	}
	gate.release();
	if (auto coordinator = weak.lock())
		coordinator->close();
	std::rethrow_exception(failure);
}
void exercise(bool injected, bool settled = false) {
	const auto root = directory(::getpid());
	std::filesystem::create_directories(root);
	std::filesystem::permissions(root, std::filesystem::perms::owner_all);
	auto account = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
	d::RetirementRuntime runtime(account, 1);
	auto domain = runtime.domain(account, 64);
	asio::io_context io(1);
	asio::thread_pool worker(1);
	Gate gate(worker);
	l::DiskStoreOptions disk;
	disk.mode = l::StoreOpenMode::FreshEmpty;
	disk.limits = {1024 * 1024, 100, 10};
	auto store = l::make_disk_store(domain, root, account, disk);
	auto queue = kronuz::io::completion::make_native_queue(
		kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
	auto driver = std::allocate_shared<t::NativeDriver>(d::StorageAllocator<t::NativeDriver>(account),
														io.get_executor(), queue);
	t::CoordinatorOptions options;
	options.directory = root;
	options.source.endpoint = {asio::ip::address_v4::loopback(), 8317};
	options.source.hostname = "localhost";
	options.source.token = "0123456789abcdef0123456789abcdef";
	options.source.configured.cluster.fill('c');
	options.source.configured.group.fill('g');
	options.source.configured.restore_epoch.fill('e');
	options.source.configured.scope.fill('s');
	options.source.tls_context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
	options.source.tls_context->set_verify_mode(asio::ssl::verify_peer);
	auto coordinator = t::make_coordinator(options, store, driver, domain, worker);
	const auto original_failure = std::make_exception_ptr(std::runtime_error("original worker failure"));
	if (settled) {
		t::detail::CoordinatorAccess::fail_worker_and_wait_after_ready(*coordinator, original_failure);
		gate.release();
	} else if (injected)
		t::detail::CoordinatorAccess::fail_next_worker_wait(*coordinator);
	std::weak_ptr<t::Coordinator> weak = coordinator;
	auto original = coordinator->run();
	auto done = asio::co_spawn(io, std::move(original), asio::use_future);
	std::future<void> observed;
	if (!injected && !settled)
		observed = asio::co_spawn(io, observe(weak, gate), asio::use_future);
	auto retained = settled ? coordinator : nullptr;
	coordinator.reset();
	io.run();
	if (settled) {
		done.get();
		check(retained->status().error == original_failure && !retained->status().worker_pending);
		retained.reset();
	} else {
		done.get();
		if (!injected)
			observed.get();
	}
	check(!injected && weak.expired() && store->drained());
	worker.join();
	driver.reset();
	queue.reset();
	store.reset();
	domain.close().wait();
	std::filesystem::remove_all(root);
}
} // namespace
int main(int argc, char **argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--fault") {
			struct rlimit limit{0, 0};
			check(::setrlimit(RLIMIT_CORE, &limit) == 0);
			exercise(true);
			return 1;
		}
		check(argc == 1);
		const auto child = ::fork();
		check(child >= 0);
		if (!child) {
			::execl(argv[0], argv[0], "--fault", nullptr);
			::_exit(127);
		}
		int status = 0;
		check(::waitpid(child, &status, 0) == child);
		check(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
		std::filesystem::remove_all(directory(child));
		exercise(false);
		exercise(false, true);
		std::cout << "held accepted worker survives close/facade loss; owner wait failure fails stop\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
