#include "detent_delivery/coordinator.hh"
#include "detent_distribution/memory_resource.hh"
#include "prepared_reader_fixture.hh"
#include "paged_plan.hh"
#include "coordinator_internal.hh"
#include "task_support.hh"
#include <asio.hpp>
#include <future>
#include <sys/wait.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace t = detent::delivery;
namespace x = l::detail;
using prepared_test::check;
using namespace std::chrono_literals;

// The coordinator starts from a store with its records, is ready as it stands, and its scrub task completes
// passes over the selected generation: clean when the store is, reporting the damaged pack when it is not.
task::task<void> await_prepared(task::asio_scheduler &scheduler, std::shared_ptr<t::Coordinator> coordinator,
                                bool damaged) {
	asio::steady_timer timer(scheduler.executor());
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
		if (prepared && state.failures >= 2 && state.scrub.passes >= 2)
			break;
		timer.expires_after(1ms);
		co_await detent::transport::await_timer(timer);
	}
	const auto state = coordinator->status();
	const bool ready = prepared && stable && state.prepared_ready && state.failures >= 2;
	// Two completed passes: the first is not a running total of the second.
	const bool scrubbed = state.scrub.passes >= 2 && state.scrub.bytes_hashed > 0 &&
		(damaged ? state.scrub.source_failure && state.scrub.damaged_packs == 1
		         : !state.scrub.source_failure && state.scrub.damaged_packs == 0);
	// A damaged pack asks for a repair once, however many passes report it; a clean store asks for none.
	const bool repair = damaged ? state.repair.pending && state.repair.requested == 1 && !state.repair.completed
	                            : !state.repair.pending && !state.repair.requested;
	coordinator->close();
	check(!coordinator->status().prepared_ready);
	check(ready && scrubbed && repair);
}
task::task<void> run(task::asio_scheduler &scheduler, std::shared_ptr<t::Coordinator> coordinator, bool damaged) {
	co_await task::when_all(coordinator->run(), await_prepared(scheduler, coordinator, damaged));
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
	check(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--scrub-damage"));
	const bool damaged = argc == 3;
	auto fixture = std::make_shared<prepared_test::Fixture>();
	prepared_test::write_file(fixture->path / "owner.lock", {});
	if (damaged) {
		// One byte of a pack's page flips before the agent starts: the restart trusts it, the scrub finds it.
		const x::SegmentNames pack(fixture->definitions[0].segment);
		const int fd = ::open((fixture->path / pack.payload_name()).c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
		check(fd >= 0);
		const auto offset = fixture->definitions[0].offset + l::paged::page_header_bytes;
		char byte;
		check(::pread(fd, &byte, 1, offset) == 1);
		byte ^= 1;
		check(::pwrite(fd, &byte, 1, offset) == 1 && ::close(fd) == 0);
	}
	auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	d::RetirementRuntime runtime(account, 2);
	auto domain = runtime.domain(account, 512);
	{
		asio::io_context io;
		task::asio_scheduler scheduler(io.get_executor());
		asio::ip::tcp::socket unavailable(io);
		unavailable.open(asio::ip::tcp::v4());
		unavailable.bind({asio::ip::address_v4::loopback(), 0});
		asio::thread_pool worker(1);
		auto queue = kronuz::io::completion::make_native_queue(
		    kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
		auto driver = std::make_shared<t::NativeDriver>(scheduler, queue);
		t::CoordinatorOptions options;
		options.source.scheduler = &scheduler;
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
		options.scrub_bytes_per_second = 1000 * 1000 * 1000;
		l::DiskStoreOptions disk;
		disk.limits = {64 * l::paged::segment_bytes, 100, 10};
		disk.recovery_limits = options.view_limits;
		auto store = l::make_disk_store(domain, fixture->path, account, disk);
		auto coordinator = t::make_coordinator(options, store, driver, domain, worker);
		auto done = spawn_future(scheduler, run(scheduler, coordinator, damaged));
		io.run();
		done.get();
		worker.join();
		check(coordinator->drained() && store->drained());
		const auto selected = store->selection().descriptor;
		// A store with its records is ready as it stands: nothing is republished.
		check(selected && selected->generation == fixture->generation && selected->cursor == fixture->root.cursor);
		if (!damaged) {
		auto prepared = x::PreparedReader::open(fixture->path, ::getuid(), options.view_limits);
		check(prepared.descriptor == *selected && prepared.view.get("a", "k").value() == 42);
		check_cli(argv[1], fixture->path, true);
		const auto metadata = fixture->path / (l::generation_name(selected->generation) + ".read");
		check(std::filesystem::remove(metadata));
		prepared_test::write_file(metadata, std::array<char, l::prepared_metadata_bytes>{});
		check_cli(argv[1], fixture->path, false);
		check(std::filesystem::remove(metadata));
		const auto actual = l::recover_generation(fixture->path, options.view_limits);
		prepared_test::write_file(metadata, x::encode_reader_metadata(x::PreparedReader::describe(actual)));
		const x::SegmentNames pack(fixture->definitions[0].segment);
		const auto payload = fixture->path / pack.payload_name();
		const int fd = ::open(payload.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
		check(fd >= 0);
		const auto offset = fixture->definitions[0].offset + l::paged::page_header_bytes;
		char byte;
		check(::pread(fd, &byte, 1, offset) == 1);
		byte ^= 1;
		check(::pwrite(fd, &byte, 1, offset) == 1 && ::close(fd) == 0);
		check_cli(argv[1], fixture->path, false);
		}
	}
	domain.close().wait();
}
