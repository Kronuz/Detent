#include "prepared_reader_fixture.hh"
#include "managed_host_internal.hh"
#include "detent_distribution/memory_resource.hh"
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <future>
#include <string_view>
#include <utility>

namespace d = detent::distribution;
namespace l = detent::local;
namespace t = detent::delivery;
using prepared_test::check;
using namespace std::chrono_literals;

struct Gate {
	std::promise<void> promise;
	bool released = false;
	void release() { if (!std::exchange(released, true)) promise.set_value(); }
	~Gate() { release(); }
};
asio::awaitable<void> observe(std::shared_ptr<t::Coordinator> coordinator, Gate &gate) {
	asio::steady_timer timer(co_await asio::this_coro::executor);
	const auto admission_deadline = std::chrono::steady_clock::now() + 5s;
	while (!coordinator->status().worker_pending && std::chrono::steady_clock::now() < admission_deadline) {
		timer.expires_after(1ms);
		co_await timer.async_wait(asio::use_awaitable);
	}
	if (!coordinator->status().worker_pending) {
		gate.release();
		coordinator->close();
		throw std::runtime_error("startup worker was not admitted");
	}
	bool announced = false, expired = false;
	const auto start = std::chrono::steady_clock::now();
	auto release = [&]() -> asio::awaitable<void> {
		timer.expires_after(40ms);
		co_await timer.async_wait(asio::use_awaitable);
		gate.release();
	};
	auto monitor = [&]() -> asio::awaitable<void> {
		try {
			co_await detent_agent::detail::monitor(coordinator, [&] { announced = true; }, true, 5ms);
		} catch (const std::runtime_error &error) {
			expired = std::string_view(error.what()).find("preparation timed out") != std::string_view::npos;
		}
	};
	using namespace asio::experimental::awaitable_operators;
	co_await (monitor() && release());
	check(expired && !announced && coordinator->drained() && !coordinator->status().worker_pending);
	check(std::chrono::steady_clock::now() - start >= 40ms);
}
asio::awaitable<void> run(std::shared_ptr<t::Coordinator> coordinator, Gate &gate) {
	using namespace asio::experimental::awaitable_operators;
	co_await (coordinator->run() && observe(coordinator, gate));
}
int main() {
	auto fixture = std::make_shared<prepared_test::Fixture>();
	prepared_test::write_file(fixture->path / "owner.lock", {});
	auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	d::RetirementRuntime runtime(account, 2);
	auto domain = runtime.domain(account, 512);
	{
		asio::io_context io;
		asio::thread_pool worker(1);
		Gate gate;
		const auto release = gate.promise.get_future().share();
		asio::post(worker, [release] { release.wait(); });
		auto queue = kronuz::io::completion::make_native_queue(
			kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
		auto driver = std::make_shared<t::NativeDriver>(io.get_executor(), queue);
		t::CoordinatorOptions options;
		options.directory = fixture->path;
		options.source.configured = fixture->root.cursor;
		options.source.endpoint = {asio::ip::address_v4::loopback(), 1};
		options.source.hostname = "localhost";
		options.source.token = "prepared-deadline-0123456789abcdef";
		options.source.tls_context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
		options.view_limits.retirement_domain = &domain;
		options.view_limits.mapping_account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
		l::DiskStoreOptions disk;
		disk.limits = {64 * l::paged::segment_bytes, 100, 10};
		disk.recovery_limits = options.view_limits;
		auto store = l::make_disk_store(domain, fixture->path, account, disk);
		auto coordinator = t::make_coordinator(options, store, driver, domain, worker);
		auto done = asio::co_spawn(io, run(coordinator, gate), asio::use_future);
		io.run();
		done.get();
		worker.join();
		check(store->drained() && !coordinator->status().prepared_ready);
	}
	domain.close().wait();
}
