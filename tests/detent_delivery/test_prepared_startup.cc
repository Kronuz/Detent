#include "detent_delivery/coordinator.hh"
#include "detent_distribution/memory_resource.hh"
#include "prepared_reader_fixture.hh"
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <future>
#include <sys/wait.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace t = detent::delivery;
namespace x = l::detail;
using prepared_test::check;
using namespace std::chrono_literals;

asio::awaitable<void> await_prepared(std::shared_ptr<t::Coordinator> coordinator) {
	asio::steady_timer timer(co_await asio::this_coro::executor);
	const auto deadline = std::chrono::steady_clock::now() + 10s;
	std::optional<l::Descriptor> prepared;
	bool stable = true;
	while (!coordinator->drained() && std::chrono::steady_clock::now() < deadline) {
		const auto state = coordinator->status();
		if (!prepared && state.prepared_ready)
			prepared = state.selected.descriptor;
		if (prepared && (!state.prepared_ready || state.selected.descriptor != prepared)) {
			stable = false;
			break;
		}
		if (prepared && state.failures >= 2)
			break;
		timer.expires_after(1ms);
		co_await timer.async_wait(asio::use_awaitable);
	}
	const bool ready = prepared && stable && coordinator->status().prepared_ready &&
		coordinator->status().failures >= 2;
	coordinator->close();
	check(!coordinator->status().prepared_ready);
	check(ready);
}
asio::awaitable<void> run(std::shared_ptr<t::Coordinator> coordinator) {
	using namespace asio::experimental::awaitable_operators;
	co_await (coordinator->run() && await_prepared(coordinator));
}
void check_cli(const char *reader, const std::filesystem::path &directory, bool valid) {
	int output[2];
	check(::pipe(output) == 0);
	const auto uid = std::to_string(::geteuid());
	const auto child = ::fork();
	check(child >= 0);
	if (!child) {
		::close(output[0]);
		::dup2(output[1], STDOUT_FILENO);
		::close(output[1]);
		::execl(reader, reader, "get", "a:k", "--generation-dir", directory.c_str(), "--publisher-user",
		        uid.c_str(), "--default", "fallback", nullptr);
		::_exit(127);
	}
	::close(output[1]);
	std::string result;
	char buffer[128];
	for (;;) {
		const auto count = ::read(output[0], buffer, sizeof(buffer));
		if (count < 0 && errno == EINTR)
			continue;
		check(count >= 0);
		if (!count)
			break;
		result.append(buffer, count);
	}
	::close(output[0]);
	int status = 0;
	while (::waitpid(child, &status, 0) < 0)
		check(errno == EINTR);
	check(WIFEXITED(status));
	check(valid ? (WEXITSTATUS(status) == 0 && result == "42\n") : WEXITSTATUS(status) != 0);
}
int main(int argc, char **argv) {
	check(argc == 2);
	auto fixture = std::make_shared<prepared_test::Fixture>();
	prepared_test::write_file(fixture->path / "owner.lock", {});
	check(std::filesystem::remove(fixture->path / (l::generation_name(fixture->generation) + ".read")));
	auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	d::RetirementRuntime runtime(account, 2);
	auto domain = runtime.domain(account, 512);
	{
		asio::io_context io;
		asio::ip::tcp::socket unavailable(io);
		unavailable.open(asio::ip::tcp::v4());
		unavailable.bind({asio::ip::address_v4::loopback(), 0});
		asio::thread_pool worker(1);
		auto queue = kronuz::io::completion::make_native_queue(
		    kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
		auto driver = std::make_shared<t::NativeDriver>(io.get_executor(), queue);
		t::CoordinatorOptions options;
		options.directory = fixture->path;
		options.image_format = t::LocalImageFormat::PagedV2;
		options.source.configured = fixture->root.cursor;
		options.source.endpoint = unavailable.local_endpoint();
		options.source.hostname = "localhost";
		options.source.token = "prepared-startup-0123456789abcdef";
		options.source.tls_context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		options.view_limits.retirement_domain = &domain;
		options.view_limits.mapping_account =
		    std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
		l::DiskStoreOptions disk;
		disk.limits = {64 * l::paged::segment_bytes, 100, 10};
		disk.recovery_limits = options.view_limits;
		auto store = l::make_disk_store(domain, fixture->path, account, disk);
		auto coordinator = t::make_coordinator(options, store, driver, domain, worker);
		auto done = asio::co_spawn(io, run(coordinator), asio::use_future);
		io.run();
		done.get();
		worker.join();
		check(coordinator->drained() && store->drained());
		const auto selected = store->selection().descriptor;
		check(selected && selected->generation != fixture->generation &&
		      selected->cursor == fixture->root.cursor);
		auto prepared = x::PreparedReader::open(fixture->path, ::getuid(), options.view_limits);
		check(prepared.descriptor == *selected && prepared.view.get("a", "k").value() == 42);
		check_cli(argv[1], fixture->path, true);
		const auto metadata = fixture->path / (l::generation_name(selected->generation) + ".read");
		check(std::filesystem::remove(metadata));
		prepared_test::write_file(metadata, std::array<char, l::prepared_metadata_bytes>{});
		check_cli(argv[1], fixture->path, false);
	}
	domain.close().wait();
}
