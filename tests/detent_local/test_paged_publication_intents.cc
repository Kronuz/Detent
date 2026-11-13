#include "applied_delta_fixture.hh"
#include "detent_local/disk_store.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include "paged_maintenance_plan.hh"
#include "paged_compaction_plan.hh"
void drive(const std::shared_ptr<l::DiskStore> &store) {
	// The store's work runs here, as the coordinator's worker runs it; advance() settles it.
	for (unsigned turns = 0;; ++turns) {
		require(turns < 100000);
		store->advance();
		auto work = store->work();
		if (!work)
			break;
		work->run();
	}
}
l::DiskUsage census(const std::filesystem::path &root) {
	l::DiskUsage usage;
	for (const auto &entry : std::filesystem::directory_iterator(root)) {
		usage.bytes += entry.file_size();
		++usage.entries;
		if (entry.path().extension() == ".lease")
			++usage.generations;
	}
	return usage;
}
// Publishes one paged intent over an installed base, or cancels it before its work is taken.
void run(const std::filesystem::path &root, const l::detail::PagedImage &image, unsigned intent, bool cancel) {
	install(root, image);
	auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 64);
	const auto initial = account->used();
	l::DiskStoreOptions options;
	options.limits = {64 * l::paged::segment_bytes, 100, 10};
	options.recovery_limits.retirement_domain = &domain;
	options.recovery_limits.mapping_account =
		std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	auto base = std::optional<l::RecoveredGeneration>(l::recover_generation(root, options.recovery_limits));
	std::optional<l::detail::PagedPlan> plan;
	if (intent == 1) {
		auto delta = std::make_shared<const d::ValidatedDelta>(parse_delta(true, 2, "app", account));
		l::detail::PagedPlanBuilder builder(*base, delta, domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		plan.emplace(builder.take());
	} else if (!intent) {
		l::detail::PagedDeltaPlanBuilder builder(*base, parse_delta(false, 2, "app", account), domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		plan.emplace(builder.take());
	} else if (intent == 2) {
		l::detail::PagedMaintenanceBuilder builder(*base, domain.factory(), account,
												   options.recovery_limits);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		plan.emplace(builder.take());
	} else {
		const auto selected = image.segments().front().ownership.segment;
		l::detail::PagedCompactionBuilder builder(*base, {&selected, 1}, domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		plan.emplace(builder.take());
	}
	const auto wanted = plan->descriptor();
	auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
	drive(store);
	require(store->publication_ready());
	const auto before = census(root);
	store->begin(std::move(*plan));
	if (cancel) {
		store->cancel_publication();
		require(store->publication_status() == l::DiskPublicationStatus::Cancelled);
		require(census(root) == before && store->publication_ready());
	}
	drive(store);
	const auto stats = store->stats();
	require(store->publication_status() ==
			(cancel ? l::DiskPublicationStatus::Cancelled : l::DiskPublicationStatus::Published));
	require(stats.used == census(root) && stats.reserved == l::DiskUsage{});
	{
		auto recovered = l::recover_generation(root, options.recovery_limits);
		require(recovered.descriptor() == (cancel ? image.descriptor() : wanted));
		if (store->publication_status() == l::DiskPublicationStatus::Published) {
			require(recovered.descriptor() == wanted && !store->publication_ready());
			store->confirm_recovered(recovered);
			require(store->publication_ready());
		}
	}
	// Once confirmed and let go, the replaced base is collected.
	base.reset();
	store->request_collection();
	drive(store);
	require(store->stats().used == census(root) && store->stats().used.generations == 1);
	store->close();
	drive(store);
	require(store->drained());
	store.reset();
	plan.reset();
	domain.close().wait();
	require(account->used() == initial && options.recovery_limits.mapping_account->used() == 0);
}
int main() {
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("paged-publication-faults-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::GenerationId generation{};
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	unsigned cases = 0;
	for (unsigned intent : {0u, 1u, 2u, 3u})
		for (bool cancel : {false, true})
			run(directory / std::to_string(cases++), image, intent, cancel);
	std::cout << cases << " paged publications (delta, progress, maintenance, compaction) and cancellations "
				 "passed\n";
}
