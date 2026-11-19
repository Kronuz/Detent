#include "host.hh"
#include "../detent_cli/logging.hh"
#include "detent/owner_loop.hh"
#include "detent/scope.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_local/directory.hh"
#include "detent_relay/configuration.hh"
#include "detent_relay/http_upstream.hh"
#include "detent_relay/service.hh"
#include <asio.hpp>
#include <cassert>
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
	std::uint64_t reported_failures = 0;
	bool cleanup_pending = true; // a publication or a fence may leave stale cache edges
	BundleState(d::RetirementRuntime &retirement, std::shared_ptr<d::StorageLedger> pub,
				std::shared_ptr<d::StorageLedger> cached, std::shared_ptr<d::StorageLedger> mapped,
				std::size_t sessions)
		: publication(std::move(pub)), cache(std::move(cached)), mapping(std::move(mapped)),
		  publication_domain(retirement.domain(publication, 4096)),
		  cache_domain(retirement.domain(cache, cache_owner_capacity(sessions))) {}
};
// The bundle as its coordinator's publication sink: every call may give the turn work (a ready bundle,
// a snapshot to encode, parked sessions to answer, stale edges, a fence), so each one wakes it.
class WakingSink final : public t::PublicationSink {
  public:
	WakingSink(std::shared_ptr<t::PublicationSink> inner, OwnerWake wake)
		: inner_(std::move(inner)), wake_(std::move(wake)) {}
	void authenticated() noexcept override {
		inner_->authenticated();
		wake_.raise();
	}
	void begin_stage(const t::ReceivedUpdate &update) override {
		inner_->begin_stage(update);
		wake_.raise();
	}
	bool stage_step(d::WorkBudget &budget) override {
		const bool done = inner_->stage_step(budget);
		wake_.raise();
		return done;
	}
	void bind_candidate(const l::Descriptor &descriptor, l::DirectoryIdentity directory) noexcept override {
		inner_->bind_candidate(descriptor, directory);
		wake_.raise();
	}
	bool release_stage(d::WorkBudget &budget) noexcept override {
		const bool done = inner_->release_stage(budget);
		wake_.raise();
		return done;
	}
	void selected(const l::RecoveredGeneration &generation) override {
		inner_->selected(generation);
		wake_.raise();
	}
	void selected_equivalent(const l::RecoveredGeneration &generation,
							 const l::detail::StateEquivalenceProof &proof) override {
		inner_->selected_equivalent(generation, proof);
		wake_.raise();
	}
	void selected_repaired(const l::RecoveredGeneration &generation) override {
		inner_->selected_repaired(generation);
		wake_.raise();
	}
	void fence() noexcept override {
		inner_->fence();
		wake_.raise();
	}

  private:
	std::shared_ptr<t::PublicationSink> inner_;
	OwnerWake wake_;
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
				const Records &records, ServiceStatus service, const MirrorService *mirror, bool ready,
				std::uint64_t turns) {
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
	field(",\"turns\":", turns);
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
	if (mirror) {
		const auto state = mirror->status();
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
		output->append("}");
	}
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
		field(",\"scrub_passes\":", delivery.scrub.passes);
		field(",\"scrub_damaged_packs\":", delivery.scrub.damaged_packs);
		field(",\"scrub_source_failure\":", delivery.scrub.source_failure);
		field(",\"sync_files\":", delivery.sync.total.files);
		field(",\"sync_groups\":", delivery.sync.total.groups);
		field(",\"sync_directory_syncs\":", delivery.sync.total.directory_syncs);
		field(",\"sync_device_flushes\":", delivery.sync.total.device_flushes);
		field(",\"startup_device_flushes\":", delivery.sync.startup.device_flushes);
		field(",\"publication_files\":", delivery.sync.publication.files);
		field(",\"publication_groups\":", delivery.sync.publication.groups);
		field(",\"publication_directory_syncs\":", delivery.sync.publication.directory_syncs);
		field(",\"publication_device_flushes\":", delivery.sync.publication.device_flushes);
		field(",\"hash_page_bytes\":", delivery.hash.hashed().page_bytes);
		field(",\"hash_pack_bytes\":", delivery.hash.hashed().pack_bytes);
		field(",\"hash_metadata_bytes\":", delivery.hash.hashed().metadata_bytes);
		field(",\"hash_frame_bytes\":", delivery.hash.frame.bytes);
		field(",\"hash_built_bytes\":", delivery.hash.built_bytes());
		field(",\"hash_checked_bytes\":", delivery.hash.checked_bytes());
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
static int run_host_owned(const std::filesystem::path &configuration, bool system, const detent_cli::Ready &ready) {
	OwnerLoop owner;
	auto &wake = owner.wake();
	asio::thread_pool startup(1);
	auto options = on_worker<HostOptions>(startup, [&] { return load_host_options(configuration, system); });
	auto root = std::make_shared<d::StorageLedger>(options.heap_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(root, options.baseline_bytes);
	auto control = std::allocate_shared<d::StorageLedger>(d::StorageAllocator<d::StorageLedger>(baseline),
														  root, options.control_bytes);
	d::RetirementRuntime retirement(baseline, std::max<std::size_t>(1, options.bundles.size() * 2));
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
														owner.scheduler(), queue, true);
	// W8: the namespace mirror, a reader of the authority or an upstream relay, never a member.
	std::shared_ptr<MirrorService> mirror;
	if (options.mirror) {
		auto &configured = *options.mirror;
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
		configured.source.coordinator.source.scheduler = &owner.scheduler();
		configured.source.coordinator.publication_sink = std::make_shared<WakingSink>(record.bundle, wake);
		configured.source.coordinator.view_limits.retirement_domain = &record.publication_domain;
		configured.source.coordinator.view_limits.mapping_account = record.mapping;
		configured.source.disk.recovery_limits = configured.source.coordinator.view_limits;
	}
	auto registry = std::allocate_shared<Registry>(d::StorageAllocator<Registry>(baseline), baseline,
												   registry_bindings, options.credentials);
	registry_bindings.clear();
	names.clear();
	auto runtime =
		std::allocate_shared<ServiceRuntime>(d::StorageAllocator<ServiceRuntime>(baseline), accounts, registry,
											 session_bindings, options.service_limits, mirror, [wake] { wake.raise(); });
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
		if (mirror)
			mirror->close();
		for (auto &record : records)
			record->coordinator->close();
		stop.set_value();
		wake.raise();
	};
	asio::signal_set signals(owner.io(), SIGINT, SIGTERM);
	signals.async_wait([&](const asio::error_code &error, int) {
		if (!error)
			shutdown();
	});
	// The turn runs while it changes anything or a sweep is unfinished, then sleeps on `tick` until the next
	// health report or snapshot retry, or until a wake: a request, a reply, a mirror answer, a publication.
	asio::steady_timer tick(owner.io());
	std::size_t bundle_cursor = 0, unvisited_bundles = 0;
	bool rescan = true;
	std::uint64_t turns = 0;
	std::chrono::steady_clock::time_point health_due{};
	std::function<void()> turn;
	auto sleep = [&](std::chrono::steady_clock::time_point until) {
		rescan = true;
		wake.arm(tick, until);
		tick.async_wait([&](const asio::error_code &error) {
			if (!error || error == asio::error::operation_aborted)
				turn();
		});
	};
	turn = [&] {
		++turns;
		try {
			if (wake.begin_turn())
				rescan = true;
			const auto now = std::chrono::steady_clock::now();
			bool progress = runtime->step(now, rescan);
			if (rescan) {
				unvisited_bundles = records.size();
				for (auto &record : records)
					record->cleanup_pending = true;
			}
			for (std::size_t visits = 0; visits < std::min<std::size_t>(8, records.size()); ++visits) {
				unvisited_bundles -= std::min<std::size_t>(unvisited_bundles, 1);
				auto &record = *records[bundle_cursor++ % records.size()];
				const auto state = record.coordinator->status();
				if (state.failures != record.reported_failures) {
					std::string detail;
					if (state.error) {
						try {
							std::rethrow_exception(state.error);
						} catch (const std::exception &error) {
							detail = error.what();
						} catch (...) {
							detail = "unknown failure";
						}
					}
					L_WARNING("detent-relay: bundle {} delivery failures {}, preserving durable selection: {}",
							  record.directory.string(), state.failures, detail);
					record.reported_failures = state.failures;
				}
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
				if (record.cleanup_pending) {
					d::WorkBudget cleanup;
					record.cleanup_pending = !record.bundle->cleanup_step(cleanup);
				}
			}
			if (mirror && !stop_requested)
				if (const auto state = mirror->status(); state.failed) {
					L_WARNING("detent-relay: namespace mirror stopped: {}", state.error);
					failure = std::make_exception_ptr(std::runtime_error("relay mirror: " + state.error));
					shutdown();
				}
			if (!announced && !stop_requested && (!mirror || mirror->status().ready) &&
				std::all_of(records.begin(), records.end(),
							[](const auto &record) { return record->bundle->status().ready; })) {
				runtime->health(
					health_snapshot(accounts.metadata, *root, records, runtime->status(), mirror.get(), true, turns));
				health_due = now + std::chrono::milliseconds(100);
				ready();
				announced = true;
				L_INFO("detent-relay: {} durable bundles{} ready on {}:{}", records.size(),
					   mirror ? " and the namespace mirror" : "", options.endpoint.address().to_string(),
					   listener.port());
			}
			if (now >= health_due) {
				try {
					runtime->health(health_snapshot(accounts.metadata, *root, records, runtime->status(),
													mirror.get(), announced && !stop_requested, turns));
				} catch (const d::StoragePressure &) {
				}
				health_due = now + std::chrono::milliseconds(100);
			}
			if (listener_stopped && runtime->drained() &&
				std::all_of(records.begin(), records.end(),
							[](const auto &record) { return record->coordinator->drained(); })) {
				signals.cancel();
				wake.release();
				owner.io().stop();
				owner_done.set_value();
				return;
			}
			const bool cleaning = std::any_of(records.begin(), records.end(),
											  [](const auto &record) { return record->cleanup_pending; });
			if (progress || !runtime->swept() || unvisited_bundles || cleaning) {
				rescan = progress;
				asio::post(owner.io(), turn);
			} else {
				auto until = health_due;
				for (const auto &record : records)
					if (record->snapshot_retry > now)
						until = std::min(until, record->snapshot_retry);
				sleep(until);
			}
		} catch (...) {
			failure = std::current_exception();
			shutdown();
			// One retry at once (shutdown raised the wake); a turn that keeps failing retries every 100 ms.
			sleep(std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
		}
	};
	// The coordinators run on the owner's scheduler, admitted from the owner thread.
	asio::post(owner.io(), [&] {
		try {
			asio::post(owner.io(), turn);
			for (auto &record : records)
				task::spawn(task::scheduler_ref::of(owner.scheduler()), record->coordinator->run(),
							[&](std::exception_ptr error) {
								if (error) {
									failure = error;
									shutdown();
								}
								wake.raise(); // a drained coordinator may let the owner stop
							});
		} catch (...) {
			// Unadmitted run originals cannot participate in graceful drainage.
			std::terminate();
		}
	});
	stopping.get();
	listener.stop();
	asio::post(owner.io(), [&] {
		listener_stopped = true;
		wake.raise();
	});
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
	session_bindings.clear();
	registry.reset();
	for (auto &record : records) {
		record->coordinator.reset();
		record->bundle.reset();
		record->store.reset();
	}
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
int run_host(const std::filesystem::path &configuration, bool system, const detent_cli::Ready &ready) {
	// The io_context may hold a cancelled deadline's handler until it is destroyed, so the check
	// follows the owned run (T13).
	[[maybe_unused]] const auto frames = task::detail::live_frames();
	const int result = run_host_owned(configuration, system, ready);
	assert(task::detail::live_frames() == frames && "a task frame outlived the relay host (T13)");
	return result;
}
} // namespace detent::relay
