#include "applied_delta_fixture.hh"
#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <future>
#include <thread>
namespace c = kronuz::io::completion;
void drive(const std::shared_ptr<l::DiskStore> &store) {
	for (unsigned turns = 0; store->operation() || store->needs_work(); ++turns) {
		require(turns < 100000);
		if (auto operation = store->operation()) {
			const auto &request = operation->request();
			operation->submitted();
			require(
				operation->complete(kronuz::journal::detail::execute_primitive(operation->io(), request)));
		}
		store->advance();
	}
}
template <class F> void wait_for(F predicate) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!predicate()) {
		require(std::chrono::steady_clock::now() < deadline);
		std::this_thread::yield();
	}
}
void run(std::uint64_t available, bool close_during_worker) {
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("managed-recovery-failure-" + std::to_string(::getpid()) + "-" +
							std::to_string(available) + "-" + std::to_string(close_during_worker));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{directory};
	l::StorageIdentity storage{}, generation{}, next{};
	storage.fill('s');
	generation.fill('g');
	next.fill('n');
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::detail::PagedSnapshotBuilder snapshot(source, storage, generation);
	while (!snapshot.done()) {
		l::ValidationBudget budget;
		snapshot.step(budget);
	}
	auto image = snapshot.take();
	install(directory, image, storage);
	write(directory / "owner.lock", {});
	auto heap = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
	auto mappings = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
	auto controls = std::make_shared<d::StorageLedger>(1024 * 1024);
	d::RetirementRuntime runtime(controls, 1);
	auto domain = runtime.domain(heap, 256);
	{
		l::DiskStoreOptions options;
		options.limits = {64 * l::paged::segment_bytes, 100, 10};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = mappings;
		auto base = l::recover_generation(directory, options.recovery_limits);
		const auto baseline = mappings->used();
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		l::detail::PagedDeltaPlanBuilder builder(base, one_key_delta(heap), next, domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		std::optional<l::detail::PagedPlan> plan(builder.take());
		const auto wanted = plan->descriptor();
		store->begin(std::move(*plan));
		plan.reset();
		drive(store);
		require(store->publication_status() == l::DiskPublicationStatus::Published);
		std::optional<l::detail::ManagedRecoveryContext> context(l::detail::ManagedRecovery::capture(*store));
		auto metrics = std::make_shared<l::detail::RecoveryMetrics>();
		context->paged.metrics = metrics;
		auto pressure = heap->admit(heap->limit() - heap->used() - available);
		std::exception_ptr original;
		std::promise<void> arrived, resume;
		auto ready = arrived.get_future(), release = resume.get_future();
		std::thread worker([&, work = *context]() mutable {
			try {
				l::detail::ManagedRecovery::recover(directory, std::move(work));
			} catch (...) {
				original = std::current_exception();
			}
			arrived.set_value();
			release.wait();
		});
		ready.get();
		if (close_during_worker) {
			store->close();
			drive(store);
			require(store->drained() && mappings->used() > baseline);
		}
		resume.set_value();
		worker.join();
		require(original && context->paged.admission->failure() == original);
		require(metrics->root_mapping_installs == 1);
		require(metrics->segment_mapping_installs == (available ? 1 : 0));
		try {
			l::detail::ManagedRecovery::recover(directory / "missing", *context);
			throw std::runtime_error("consumed failed recovery was replayed");
		} catch (...) {
			require(std::current_exception() == original);
		}
		pressure.reset();
		{
			auto stale = *context;
			stale.paged.admission =
				domain.factory().make<l::detail::MappedRecoveryAdmission>(heap, mappings, domain.factory());
			if (!close_during_worker) {
				store->close();
				drive(store);
			}
			try {
				l::detail::ManagedRecovery::settle_failure(
					*store, stale, std::make_exception_ptr(std::runtime_error("stale failure")));
				throw std::runtime_error("stale recovery poisoned closing store");
			} catch (const std::invalid_argument &) {
				require(!store->error());
			}
		}
		l::detail::ManagedRecovery::settle_failure(*store, *context, original);
		drive(store);
		require(store->drained() && store->error() == original);
		require(store->selection().knowledge == l::SelectionKnowledge::Known &&
				store->selection().descriptor == wanted);
		require(store->publication_status() == l::DiskPublicationStatus::Published);
		require(base.view().get("app", "count").value() == INT64_MIN);
		pressure.reset();
		context.reset();
		store.reset();
		wait_for([&] { return mappings->used() == baseline; });
		auto restarted = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(restarted);
		require(restarted->status() == l::StoreStatus::Ready && restarted->selection().descriptor == wanted);
		{
			auto recovered = l::detail::ManagedRecovery::recover(
				directory, l::detail::ManagedRecovery::capture(*restarted));
			require(recovered.descriptor() == wanted && recovered.view().get("app", "count").value() == 1);
		}
		restarted->close();
		drive(restarted);
		restarted.reset();
	}
	domain.close().wait();
	require(mappings->used() == 0);
}
int main() {
	run(0, false);
	run(80 * 1024, false);
	run(80 * 1024, true);
}
