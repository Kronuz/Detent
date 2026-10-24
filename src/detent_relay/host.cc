#include "host.hh"
#include "../detent_cli/logging.hh"
#include "detent/scope.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_local/directory.hh"
#include "detent_relay/configuration.hh"
#include "detent_relay/service.hh"
#include <asio.hpp>
#include <charconv>
#include <fcntl.h>
#include <future>
#include <http_asio.h>
#include <http_asio_tls.h>
#include <openssl/rand.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::relay {
namespace d = distribution;
namespace l = local;
namespace t = delivery;
namespace {
std::size_t cache_owner_capacity(std::size_t sessions) {
	if (sessions > SIZE_MAX - 4096) throw std::invalid_argument("relay cache owner capacity overflow");
	return 4096 + sessions;
}
struct BundleState {
	std::shared_ptr<d::StorageLedger> publication, cache, mapping;
	d::RetirementDomain publication_domain, cache_domain;
	std::shared_ptr<Bundle> bundle;
	std::shared_ptr<t::Coordinator> coordinator;
	std::shared_ptr<l::DiskStore> store;
	std::filesystem::path directory;
	std::chrono::steady_clock::time_point snapshot_retry{};
	BundleState(d::RetirementRuntime &retirement, std::shared_ptr<d::StorageLedger> pub,
				std::shared_ptr<d::StorageLedger> cached, std::shared_ptr<d::StorageLedger> mapped,
				std::size_t sessions)
		: publication(std::move(pub)), cache(std::move(cached)), mapping(std::move(mapped)),
		  publication_domain(retirement.domain(publication, 4096)),
		  cache_domain(retirement.domain(cache, cache_owner_capacity(sessions))) {}
};
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
template <class Records>
std::shared_ptr<const d::OwnedBuffer>
health_snapshot(std::shared_ptr<d::StorageLedger> account, const d::StorageLedger &root,
				const Records &records, ServiceStatus service, t::CompletionSchedulerStatus scheduling,
				kronuz::journal::CompletionStats disk, bool ready) {
	auto output = d::make_owned_buffer(std::move(account), 256 * 1024);
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
	field(",\"sessions\":", service.sessions);
	field(",\"opens\":", service.requests[0]);
	field(",\"polls\":", service.requests[1]);
	field(",\"closes\":", service.requests[2]);
	field(",\"bootstrap\":", service.bootstrap);
	field(",\"catchup\":", service.catchup);
	field(",\"peak_transfers\":", service.peak_transfers);
	field(",\"disk_queued\":", scheduling.queued);
	field(",\"disk_peak_queued\":", scheduling.peak_queued);
	field(",\"disk_grants\":", scheduling.grants);
	field(",\"disk_maximum_wait_ns\":", scheduling.maximum_wait_nanoseconds);
	field(",\"disk_native_submitted\":", disk.native_submitted);
	field(",\"disk_fallback_submitted\":", disk.fallback_submitted);
	field(",\"disk_maximum_outstanding\":", disk.maximum_outstanding);
	output->append(",\"bundles\":[");
	for (std::size_t index = 0; index < records.size(); ++index) {
		if (index)
			output->append(",");
		const auto &record = *records[index];
		const auto state = record.bundle->status();
		field("{\"index\":", index);
		field(",\"revision\":", state.selected ? state.selected->revision : 0);
		field(",\"ready\":", state.ready);
		field(",\"authenticated\":", state.authenticated);
		field(",\"fenced\":", state.fenced);
		field(",\"publication_bytes\":", record.publication->used());
		field(",\"cache_bytes\":", record.cache->used());
		field(",\"mapping_bytes\":", record.mapping->used());
		field(",\"gaps\":", state.gaps);
		field(",\"refusals\":", state.refusals);
		const auto delivery = record.coordinator->status();
		field(",\"source_endpoint_index\":", delivery.source_endpoint_index);
		field(",\"source_rotations\":", delivery.source_rotations);
		field(",\"quorum_refresh_failures\":", delivery.quorum_refresh_failures);
		if (delivery.last_quorum_open)
			field(",\"quorum_open_age_ms\":",
				  std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
																		*delivery.last_quorum_open)
					  .count());
		else
			output->append(",\"quorum_open_age_ms\":null");
		field(",\"snapshot_ready\":", record.bundle->offer({}, true).decision == d::CacheDecision::Update);
		output->append("}");
	}
	output->append("]}");
	output->seal_prefix();
	return output;
}
} // namespace
int run_host(const std::filesystem::path &configuration, bool system, const detent_cli::Ready &ready) {
	asio::io_context owner(1);
	asio::thread_pool startup(1);
	auto options = on_worker<HostOptions>(startup, [&] { return load_host_options(configuration, system); });
	auto root = std::make_shared<d::StorageLedger>(options.heap_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(root, options.baseline_bytes);
	auto control = std::allocate_shared<d::StorageLedger>(d::StorageAllocator<d::StorageLedger>(baseline),
														  root, options.control_bytes);
	d::RetirementRuntime retirement(baseline, options.bundles.size() * 2);
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
	using Records =
		std::vector<d::StorageOwner<BundleState>, d::StorageAllocator<d::StorageOwner<BundleState>>>;
	Records records{d::StorageAllocator<d::StorageOwner<BundleState>>(baseline)};
	records.reserve(options.bundles.size());
	for (const auto &configured : options.bundles) {
		auto mapping = std::allocate_shared<d::StorageLedger>(d::StorageAllocator<d::StorageLedger>(baseline),
															  configured.source.mapping_bytes);
		records.push_back(d::make_storage_owner<BundleState>(
			baseline, std::malloc, std::free, retirement, child(root, configured.source.heap_bytes),
			child(root, configured.cache_bytes), std::move(mapping), options.service_limits.sessions.sessions));
	}
	auto queue = kronuz::io::completion::make_native_queue(
		kronuz::io::completion::AllocationContext(d::make_storage_resource(baseline)));
	auto driver = std::allocate_shared<t::NativeDriver>(d::StorageAllocator<t::NativeDriver>(baseline),
														owner.get_executor(), queue, true);
	auto scheduler =
		std::allocate_shared<t::CompletionScheduler>(d::StorageAllocator<t::CompletionScheduler>(baseline),
													 owner.get_executor(), driver, baseline, records.size());
	std::vector<RegistryBundle> registry_bindings;
	std::vector<std::vector<std::string_view>> names;
	std::vector<SessionBinding, d::StorageAllocator<SessionBinding>> session_bindings{
		d::StorageAllocator<SessionBinding>(baseline)};
	names.reserve(records.size());
	registry_bindings.reserve(records.size());
	session_bindings.reserve(records.size());
	for (std::size_t index = 0; index < records.size(); ++index) {
		auto &configured = options.bundles[index];
		auto &record = *records[index];
		record.directory = configured.source.coordinator.directory;
		BundleOptions bundle_options;
		bundle_options.encoding = configured.source.coordinator.source.limits;
		bundle_options.configured = configured.source.coordinator.source.configured;
		bundle_options.namespaces = configured.source.coordinator.source.namespaces.size();
		bundle_options.retained_edges = configured.retained_edges;
		if (RAND_bytes(reinterpret_cast<unsigned char *>(bundle_options.incarnation.data()),
					   bundle_options.incarnation.size()) != 1)
			throw std::runtime_error("relay incarnation entropy");
		record.bundle = make_bundle(bundle_options, record.cache_domain);
		const d::SubscriptionPrincipal principal{
			d::canonical_scope_identity(configured.source.principal, 1, {}), configured.source.policy_epoch};
		names.emplace_back();
		for (const auto &name : configured.source.coordinator.source.namespaces)
			names.back().push_back(name);
		registry_bindings.push_back({bundle_options.configured, principal, names.back()});
		session_bindings.push_back({record.bundle, principal, bundle_options.configured});
		configured.source.coordinator.completion_port = scheduler->port(index);
		configured.source.coordinator.publication_sink = record.bundle;
		configured.source.coordinator.view_limits.retirement_domain = &record.publication_domain;
		configured.source.coordinator.view_limits.mapping_account = record.mapping;
		configured.source.disk.recovery_limits = configured.source.coordinator.view_limits;
	}
	auto registry = std::allocate_shared<Registry>(d::StorageAllocator<Registry>(baseline), baseline,
												   registry_bindings, options.credentials);
	registry_bindings.clear();
	names.clear();
	auto runtime =
		std::allocate_shared<ServiceRuntime>(d::StorageAllocator<ServiceRuntime>(baseline), accounts,
											 registry, session_bindings, options.service_limits);
	for (std::size_t index = 0; index < records.size(); ++index) {
		auto &record = *records[index];
		record.coordinator = t::prepare_coordinator(std::move(options.bundles[index].source.coordinator),
													driver, record.publication_domain, startup);
	}
	on_worker<bool>(startup, [&] {
		std::set<std::pair<std::uint64_t, std::uint64_t>> directories;
		for (std::size_t index = 0; index < records.size(); ++index) {
			auto &record = *records[index];
			const auto &configured = options.bundles[index].source;
			std::optional<l::ProvisionedDirectory> provisioned;
			if (configured.disk.mode == l::StoreOpenMode::FreshEmpty)
				provisioned.emplace(l::provision_directory(record.directory, 0700));
			struct Directory {
				int descriptor;
				~Directory() {
					if (descriptor >= 0)
						::close(descriptor);
				}
			} directory{::open(record.directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
			struct stat info{};
			if (directory.descriptor < 0 || ::fstat(directory.descriptor, &info) != 0 ||
				!S_ISDIR(info.st_mode) || info.st_uid != ::geteuid() || (info.st_mode & 0077))
				throw std::invalid_argument("relay durable roots require service ownership and mode 0700");
			const l::DirectoryIdentity opened{static_cast<std::uint64_t>(info.st_dev),
											  static_cast<std::uint64_t>(info.st_ino)};
			if (!directories.emplace(opened.device, opened.inode).second)
				throw std::invalid_argument("duplicate physical relay durable root");
			record.store = l::make_disk_store(record.publication_domain, record.directory, record.publication,
											  configured.disk);
			if (opened != record.store->directory_identity())
				throw std::runtime_error("relay private root changed while opening");
			if (provisioned) {
				provisioned->verify_name();
				if (provisioned->identity() != record.store->directory_identity())
					throw std::runtime_error("relay provisioned root changed");
			}
			record.coordinator->attach_store(record.store);
		}
		return true;
	});
	auto facade = std::make_unique<Service>(runtime);
	http::HttpAsioService listener(*facade, 2, 2, options.connections,
								   http::tls_connection_factory_timed(options.tls_context),
								   options.connections);
	http::AsioBindOptions bind;
	bind.address = options.endpoint.address().to_string();
	listener.set_bind_options(bind);
	listener.set_request_timeout(std::chrono::seconds(65));
	listener.start(options.endpoint.port());
	std::promise<void> stop;
	auto stopping = stop.get_future();
	bool stop_requested = false, listener_stopped = false, announced = false;
	std::exception_ptr failure;
	auto shutdown = [&] {
		if (stop_requested)
			return;
		stop_requested = true;
		runtime->close();
		for (auto &record : records)
			record->coordinator->close();
		stop.set_value();
	};
	asio::signal_set signals(owner, SIGINT, SIGTERM);
	signals.async_wait([&](const asio::error_code &error, int) {
		if (!error)
			shutdown();
	});
	asio::steady_timer tick(owner);
	std::size_t bundle_cursor = 0;
	std::chrono::steady_clock::time_point health_due{};
	std::function<void()> turn;
	turn = [&] {
		try {
			const auto now = std::chrono::steady_clock::now();
			bool progress = runtime->step(now);
			for (std::size_t visits = 0; visits < std::min<std::size_t>(8, records.size()); ++visits) {
				auto &record = *records[bundle_cursor++ % records.size()];
				const auto state = record.coordinator->status();
				if (state.phase == t::CoordinatorPhase::Fenced ||
					(record.coordinator->drained() && !stop_requested)) {
					failure = state.error;
					shutdown();
				}
				if (!stop_requested && now >= record.snapshot_retry) {
					const auto before = record.bundle->status();
					d::WorkBudget work;
					const bool complete = record.bundle->snapshot_step(work);
					const auto after = record.bundle->status();
					if (after.refusals != before.refusals)
						record.snapshot_retry = now + std::chrono::milliseconds(100);
					progress |= !complete && (work.tokens != 128 || work.bytes != 8192);
				}
				d::WorkBudget cleanup;
				record.bundle->cleanup_step(cleanup);
			}
			if (!announced && !stop_requested &&
				std::all_of(records.begin(), records.end(),
							[](const auto &record) { return record->bundle->status().ready; })) {
				runtime->health(health_snapshot(accounts.metadata, *root, records, runtime->status(),
												scheduler->status(), queue->stats(), true));
				health_due = now + std::chrono::milliseconds(100);
				ready();
				announced = true;
				L_INFO("detent-relay: {} durable bundles ready on {}:{}", records.size(),
					   options.endpoint.address().to_string(), options.endpoint.port());
			}
			if (now >= health_due) {
				try {
					runtime->health(health_snapshot(accounts.metadata, *root, records, runtime->status(),
													scheduler->status(), queue->stats(),
													announced && !stop_requested));
				} catch (const d::StoragePressure &) {
				}
				health_due = now + std::chrono::milliseconds(100);
			}
			if (listener_stopped && runtime->drained() &&
				std::all_of(records.begin(), records.end(),
							[](const auto &record) { return record->coordinator->drained(); })) {
				signals.cancel();
				owner.stop();
				return;
			}
			if (progress)
				asio::post(owner, turn);
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
	std::thread owner_thread([&] {
		try {
			asio::post(owner, turn);
			for (auto &record : records)
				asio::co_spawn(owner, record->coordinator->run(), [&](std::exception_ptr error) {
					if (error) {
						failure = error;
						shutdown();
					}
				});
		} catch (...) {
			// Unadmitted run originals cannot participate in graceful drainage.
			std::terminate();
		}
		owner.run();
	});
	stopping.get();
	listener.stop();
	asio::post(owner, [&] { listener_stopped = true; });
	owner_thread.join();
	startup.join();
	facade.reset();
	runtime.reset();
	session_bindings.clear();
	registry.reset();
	for (auto &record : records) {
		record->coordinator.reset();
		record->bundle.reset();
		record->store.reset();
	}
	if (!scheduler->idle())
		throw std::logic_error("relay completion scheduler did not drain");
	scheduler.reset();
	driver.reset();
	queue.reset();
	for (auto &record : records) {
		record->cache_domain.close().wait();
		record->publication_domain.close().wait();
	}
	L_NOTICE("detent-relay: drained; admitted heap peak {} bytes, configured {}", root->peak(),
			 root->limit());
	if (failure)
		std::rethrow_exception(failure);
	return 0;
}
} // namespace detent::relay
