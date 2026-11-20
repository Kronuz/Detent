#include "host.hh"
#include "../detent_cli/logging.hh"
#include "detent/owner_loop.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_relay/configuration.hh"
#include "detent_relay/http_upstream.hh"
#include "detent_relay/service.hh"
#include <asio.hpp>
#include <cassert>
#include <charconv>
#include <future>
#include <http_asio.h>
#include <http_asio_tls.h>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::relay {
namespace d = distribution;
namespace {
template <class Result, class Function> Result on_worker(asio::thread_pool &worker, Function function) {
	std::promise<Result> result;
	auto ready = result.get_future();
	asio::post(worker, [&] {
		try {
			result.set_value(function());
		} catch (...) {
			result.set_exception(std::current_exception());
		}
	});
	return ready.get();
}
std::shared_ptr<const d::OwnedBuffer> health_snapshot(std::shared_ptr<d::StorageLedger> account,
													const d::StorageLedger &root, ServiceStatus service,
													const MirrorService &mirror, bool ready) {
	auto output = d::make_owned_buffer(std::move(account), 4096);
	auto number = [&](std::uint64_t value) {
		std::array<char, 32> bytes;
		const auto converted = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
		output->append({bytes.data(), static_cast<std::size_t>(converted.ptr - bytes.data())});
	};
	auto field = [&](std::string_view name, std::uint64_t value) {
		output->append(name);
		number(value);
	};
	output->append(ready ? "{\"ready\":true" : "{\"ready\":false");
	field(",\"heap_used\":", root.used());
	field(",\"heap_peak\":", root.peak());
	field(",\"heap_limit\":", root.limit());
	field(",\"opens\":", service.requests[0]);
	field(",\"polls\":", service.requests[1]);
	field(",\"closes\":", service.requests[2]);
	const auto state = mirror.status();
	field(",\"mirror\":{\"ready\":", state.ready);
	field(",\"failed\":", state.failed);
	field(",\"syncs\":", state.syncs);
	field(",\"sync_failures\":", state.sync_failures);
	field(",\"entries\":", state.entries);
	field(",\"fetched_files\":", state.fetched_files);
	field(",\"fetched_bytes\":", state.fetched_bytes);
	field(",\"reused_files\":", state.reused_files);
	field(",\"collected_files\":", state.collected_files);
	field(",\"answers\":", state.answers);
	field(",\"idle\":", state.idle);
	field(",\"sessions\":", state.sessions);
	field(",\"failed_namespaces\":", state.failed_namespaces);
	output->append("}}");
	output->seal_prefix();
	return output;
}
} // namespace
static int run_host_owned(const std::filesystem::path &configuration, const detent_cli::Ready &ready) {
	OwnerLoop owner;
	asio::thread_pool startup(1);
	auto options = on_worker<HostOptions>(startup, [&] { return load_host_options(configuration); });
	auto root = std::make_shared<d::StorageLedger>(options.heap_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(root, options.baseline_bytes);
	auto control = std::allocate_shared<d::StorageLedger>(d::StorageAllocator<d::StorageLedger>(baseline),
														  root, options.control_bytes);
	auto child = [&](std::shared_ptr<d::StorageLedger> parent, std::size_t bytes) {
		return std::allocate_shared<d::StorageLedger>(d::StorageAllocator<d::StorageLedger>(baseline),
													  std::move(parent), bytes);
	};
	const auto open_bytes = options.control_bytes / 8;
	const auto poll_bytes = options.control_bytes * 3 / 8;
	const auto close_bytes = options.control_bytes / 32;
	ServiceAccounts accounts{
		child(control, options.control_bytes - open_bytes - poll_bytes - close_bytes),
		{child(control, open_bytes), child(control, poll_bytes), child(control, close_bytes)}};
	// W8: the namespace mirror, a reader of the authority or an upstream relay, never a member.
	std::shared_ptr<MirrorService> mirror;
	{
		auto &configured = options.mirror;
		on_worker<bool>(startup, [&] {
			const auto &directory = configured.service.directory;
			if (!std::filesystem::exists(directory)) {
				std::filesystem::create_directory(directory);
				std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			}
			struct stat info{};
			if (::stat(directory.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != ::geteuid() ||
				(info.st_mode & 0077))
				throw std::invalid_argument("relay mirror directory requires service ownership and mode 0700");
			return true;
		});
		mirror = std::make_shared<MirrorService>(configured.service,
												 std::make_unique<HttpUpstream>(configured.upstream),
												 child(root, configured.answer_bytes));
	}
	auto registry =
		std::allocate_shared<Registry>(d::StorageAllocator<Registry>(baseline), baseline, options.credentials);
	auto runtime = std::allocate_shared<ServiceRuntime>(d::StorageAllocator<ServiceRuntime>(baseline), accounts,
														registry, mirror, options.service_limits);
	auto facade = std::make_unique<Service>(runtime);
	http::HttpAsioService listener(*facade, 2, options.connections,
								   http::tls_connection_factory(options.tls_context), options.connections);
	http::AsioBindOptions bind;
	bind.address = options.endpoint.address().to_string();
	listener.set_bind_options(bind);
	listener.set_request_timeout(std::chrono::seconds(65));
	listener.start(options.endpoint.port());
	std::promise<void> stop, owner_done;
	auto stopping = stop.get_future();
	auto owner_drained = owner_done.get_future();
	bool stop_requested = false, listener_stopped = false, announced = false;
	std::exception_ptr failure;
	auto shutdown = [&] {
		if (stop_requested)
			return;
		stop_requested = true;
		runtime->close();
		mirror->close();
		stop.set_value();
	};
	asio::signal_set signals(owner.io(), SIGINT, SIGTERM);
	signals.async_wait([&](const asio::error_code &error, int) {
		if (!error)
			shutdown();
	});
	asio::steady_timer tick(owner.io());
	std::chrono::steady_clock::time_point health_due{};
	std::string sync_error; // the last sync failure logged; cleared by a successful sync
	std::function<void()> turn;
	turn = [&] {
		try {
			const auto now = std::chrono::steady_clock::now();
			const bool progress = runtime->step(now);
			const auto state = mirror->status();
			if (state.failed && !stop_requested) {
				L_WARNING("detent-relay: namespace mirror stopped: {}", state.error);
				failure = std::make_exception_ptr(std::runtime_error("relay mirror: " + state.error));
				shutdown();
			}
			if (!state.failed && state.error != sync_error) {
				if (!state.error.empty())
					L_WARNING("detent-relay: mirror sync failed: {}", state.error);
				sync_error = state.error;
			}
			if (!announced && !stop_requested && state.ready) {
				runtime->health(health_snapshot(accounts.metadata, *root, runtime->status(), *mirror, true));
				health_due = now + std::chrono::milliseconds(100);
				ready();
				announced = true;
				L_INFO("detent-relay: the namespace mirror is ready on {}:{}", options.endpoint.address().to_string(),
					   listener.port());
			}
			if (now >= health_due) {
				try {
					runtime->health(health_snapshot(accounts.metadata, *root, runtime->status(), *mirror,
													announced && !stop_requested));
				} catch (const d::StoragePressure &) {
				}
				health_due = now + std::chrono::milliseconds(100);
			}
			if (listener_stopped && runtime->drained()) {
				signals.cancel();
				owner.io().stop();
				owner_done.set_value();
				return;
			}
			if (progress)
				asio::post(owner.io(), turn);
			else {
				tick.expires_after(std::chrono::milliseconds(1));
				tick.async_wait([&](const asio::error_code &error) {
					if (!error)
						turn();
				});
			}
		} catch (...) {
			failure = std::current_exception();
			shutdown();
			tick.expires_after(std::chrono::milliseconds(1));
			tick.async_wait([&](const asio::error_code &error) {
				if (!error)
					turn();
			});
		}
	};
	asio::post(owner.io(), turn);
	stopping.get();
	listener.stop();
	asio::post(owner.io(), [&] { listener_stopped = true; });
	// Teardown (D15): the listener's loops are stopped; once the turn finds everything drained
	// the owner stops (phase 1), both close their schedulers (phase 2) and destroy their roots
	// with every io_context alive (phase 3), before anything the tasks reference is destroyed.
	owner_drained.get();
	owner.stop();
	listener.close();
	owner.close();
	listener.destroy_roots();
	owner.destroy_roots();
	startup.join();
	facade.reset();
	runtime.reset();
	mirror.reset();
	registry.reset();
	L_NOTICE("detent-relay: drained; admitted heap peak {} bytes, configured {}", root->peak(),
			 root->limit());
	if (failure)
		std::rethrow_exception(failure);
	return 0;
}
int run_host(const std::filesystem::path &configuration, const detent_cli::Ready &ready) {
	// The io_context may hold a cancelled deadline's handler until it is destroyed, so the check
	// follows the owned run (T13).
	[[maybe_unused]] const auto frames = task::detail::live_frames();
	const int result = run_host_owned(configuration, ready);
	assert(task::detail::live_frames() == frames && "a task frame outlived the relay host (T13)");
	return result;
}
} // namespace detent::relay
