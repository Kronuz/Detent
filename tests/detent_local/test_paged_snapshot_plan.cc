#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_disk_fixture.hh"
#include "paged_snapshot_plan.hh"
#include <completion/native_completion.h>
#include <source_location>
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("snapshot plan invariant at " + std::to_string(location.line()));
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	store->advance();
	for (unsigned turns = 0; store->operation() || store->needs_work(); ++turns) {
		check(turns < 1000000);
		if (auto operation = store->operation()) {
			const auto &request = operation->request();
			operation->submitted();
			check(operation->complete(kronuz::journal::detail::execute_primitive(operation->io(), request)));
		}
		store->advance();
	}
	if (store->error())
		std::rethrow_exception(store->error());
}
std::shared_ptr<const d::ValidatedSnapshot> input(std::shared_ptr<d::StorageLedger> heap,
												  std::uint64_t revision, bool reset = false,
												  unsigned percentage = 25) {
	auto wire = fixture(1, 24, percentage, revision);
	auto configured = cursor();
	if (reset) {
		const std::string old(configured.restore_epoch.data(), configured.restore_epoch.size());
		const auto offset = wire.find(old);
		check(offset != std::string::npos);
		configured.restore_epoch.fill('z');
		wire.replace(offset, old.size(),
					 std::string(configured.restore_epoch.data(), configured.restore_epoch.size()));
	}
	d::Limits limits;
	limits.storage_account = std::move(heap);
	d::SnapshotDecoder decoder(configured, {"app"}, limits);
	auto bytes = std::span<const char>(wire);
	while (!bytes.empty()) {
		d::WorkBudget budget;
		decoder.feed(bytes, budget);
	}
	return std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
}
void finish(l::detail::PagedSnapshotPlanBuilder &builder) {
	for (unsigned turns = 0; !builder.done(); ++turns) {
		check(turns < 2000000);
		l::ValidationBudget budget{1, 1};
		builder.step(budget);
	}
}
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("snapshot-plan-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{directory};
	std::filesystem::create_directories(directory);
	check(::chmod(directory.c_str(), 0700) == 0);
	auto heap = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
	auto mappings = std::make_shared<d::StorageLedger>(32 * 1024 * 1024);
	auto controls = std::make_shared<d::StorageLedger>(1024 * 1024);
	d::RetirementRuntime runtime(controls, 1);
	auto domain = runtime.domain(heap, 512);
	{
		l::DiskStoreOptions options;
		options.mode = l::StoreOpenMode::FreshEmpty;
		options.limits = {64 * l::paged::segment_bytes, 1000, 20};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = mappings;
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		check(store->selection().knowledge == l::SelectionKnowledge::Absent);
		const auto context = l::detail::ManagedRecovery::capture(*store);
		l::GenerationId generation{};
		generation.fill('a');
		for (bool wrong_storage : {false, true}) {
			auto directory_binding = context.directory;
			auto storage_binding = context.storage;
			if (wrong_storage)
				storage_binding[0] ^= 1;
			else
				++directory_binding.inode;
			l::detail::PagedSnapshotPlanBuilder wrong(input(heap, 42), {}, directory_binding, storage_binding,
													  generation, domain.factory(), heap,
													  options.recovery_limits);
			finish(wrong);
			auto candidate = wrong.take();
			const auto wanted = candidate.descriptor();
			bool rejected = false;
			try {
				store->begin(std::move(candidate));
			} catch (const std::invalid_argument &) {
				rejected = true;
			}
			check(rejected && candidate.descriptor() == wanted);
			check(store->selection().knowledge == l::SelectionKnowledge::Absent &&
				  store->stats().reserved == l::DiskUsage{});
		}

		l::detail::PagedSnapshotPlanBuilder fresh(input(heap, 42), {}, context.directory, context.storage,
												  generation, domain.factory(), heap,
												  options.recovery_limits);
		finish(fresh);
		auto plan = fresh.take();
		check(plan.intent() == l::detail::PagedPlanIntent::Fresh && !plan.selected_base() &&
			  !plan.registry());

		{
			auto pressure = mappings->admit(mappings->limit() - mappings->used());
			const auto wanted = plan.descriptor();
			bool rejected = false;
			try {
				store->begin(std::move(plan));
			} catch (const d::StoragePressure &) {
				rejected = true;
			}
			check(rejected && plan.descriptor() == wanted && store->publication_ready());
			check(store->selection().knowledge == l::SelectionKnowledge::Absent &&
				  store->stats().reserved == l::DiskUsage{});
		}
		store->begin(std::move(plan));
		drive(store);
		check(store->publication_status() == l::DiskPublicationStatus::Published);
		auto base =
			l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store));
		store->confirm_recovered(base);
		check(base.view().get("app", "count").value() == INT64_MIN);
		generation.fill('b');
		l::detail::PagedSnapshotPlanBuilder unchanged(input(heap, 42), base, context.directory,
													  context.storage, generation, domain.factory(), heap,
													  options.recovery_limits);
		finish(unchanged);
		check(unchanged.unchanged());
		auto proof = unchanged.take_unchanged_proof();
		check(store->selection().descriptor == base.descriptor());
		bool rejected = false;
		try {
			l::detail::PagedSnapshotPlanBuilder conflict(input(heap, 42, false, 26), base, context.directory,
														 context.storage, generation, domain.factory(), heap,
														 options.recovery_limits);
			finish(conflict);
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		check(rejected && store->selection().descriptor == base.descriptor());
		l::detail::PagedSnapshotPlanBuilder forward(input(heap, 43), base, context.directory, context.storage,
													generation, domain.factory(), heap,
													options.recovery_limits);
		finish(forward);
		auto next = forward.take();
		const auto wanted = next.descriptor();
		check(next.intent() == l::detail::PagedPlanIntent::Forward &&
			  next.selected_base() == base.descriptor());
		store->begin(std::move(next));
		drive(store);
		auto newer =
			l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store));
		store->confirm_recovered(newer);
		check(newer.descriptor() == wanted);
		generation.fill('d');
		rejected = false;
		try {
			l::detail::PagedSnapshotPlanBuilder backwards(input(heap, 42), newer, context.directory,
														  context.storage, generation, domain.factory(), heap,
														  options.recovery_limits);
			finish(backwards);
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		check(rejected && store->selection().descriptor == wanted);
		generation.fill('f');
		l::detail::PagedSnapshotPlanBuilder stale(input(heap, 44), base, context.directory, context.storage,
												  generation, domain.factory(), heap,
												  options.recovery_limits);
		finish(stale);
		auto stale_plan = stale.take();
		const auto stale_descriptor = stale_plan.descriptor();
		rejected = false;
		try {
			store->begin(std::move(stale_plan));
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		check(rejected && stale_plan.descriptor() == stale_descriptor &&
			  store->selection().descriptor == wanted && store->stats().reserved == l::DiskUsage{});
		generation.fill('c');
		l::detail::PagedSnapshotPlanBuilder reset(input(heap, 42, true), newer, context.directory,
												  context.storage, generation, domain.factory(), heap,
												  options.recovery_limits);
		finish(reset);
		auto reset_plan = reset.take();
		const auto reset_target = reset_plan.descriptor();
		check(reset_plan.intent() == l::detail::PagedPlanIntent::TrustedReset);
		rejected = false;
		try {
			store->begin(std::move(reset_plan));
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		check(rejected && reset_plan.descriptor() == reset_target && store->selection().descriptor == wanted);
		auto configured = reset_target.cursor;
		configured.restore_epoch.fill('q');
		rejected = false;
		try {
			store->begin_reset(std::move(reset_plan), {wanted, configured, reset_target.profile});
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		check(rejected && reset_plan.descriptor() == reset_target &&
			  store->stats().reserved == l::DiskUsage{});
		store->begin_reset(std::move(reset_plan), {wanted, reset_target.cursor, reset_target.profile});
		drive(store);
		auto restored =
			l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store));
		store->confirm_recovered(restored);
		check(restored.descriptor() == reset_target &&
			  restored.descriptor().cursor.revision < newer.descriptor().cursor.revision);
		check(base.view().get("app", "count").value() == INT64_MIN &&
			  newer.view().get("app", "count").value() == INT64_MIN);
		store->close();
		drive(store);
		check(store->drained());
	}
	domain.close().wait();
	check(mappings->used() == 0);
}
