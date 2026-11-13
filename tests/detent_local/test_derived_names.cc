// A rebuild at an unchanged head publishes under ordinal r + 1 and never reuses a name (D8.1, D8.2).
#include "detent_local/derived_ids.hh"
#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_disk_fixture.hh"
#include "paged_graph_validator.hh"
#include "paged_maintenance_plan.hh"
#include <completion/native_completion.h>
#include <set>
#include <source_location>
#include <type_traits>

// A4: every caller states the store's or the format's semantic bound.
static_assert(!std::is_default_constructible_v<detent::local::detail::PagedSemanticLimits>);
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("derived name invariant at " + std::to_string(location.line()));
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	// The store's work runs here, as the coordinator's worker runs it; advance() settles it.
	for (unsigned turns = 0;; ++turns) {
		check(turns < 100000);
		store->advance();
		auto work = store->work();
		if (!work)
			break;
		work->run();
	}
}
// The ordinal counts rebuilds at one head and resets at any other.
void ordinals() {
	l::Descriptor selected{};
	selected.generation.fill('g');
	selected.cursor.revision = 7;
	selected.ordinal = 4;
	check(l::build_ordinal(std::nullopt, selected.cursor) == 0);
	check(l::build_ordinal(selected, selected.cursor) == 5);
	auto forward = selected.cursor;
	++forward.revision;
	check(l::build_ordinal(selected, forward) == 0);
	const auto again = l::next_generation(selected, selected.cursor, selected.profile, true);
	auto next = selected;
	next.generation = again.id;
	next.ordinal = again.ordinal;
	check(again.ordinal == 5 && l::next_generation(next, next.cursor, next.profile, true).id != again.id);
	check(l::next_generation(selected, selected.cursor, selected.profile, false).id != again.id);
	// A trusted reset may change the cluster alone; at the selected revision it must not derive the
	// selected generation's name.
	auto first = selected;
	const auto from_copy = l::next_generation(std::nullopt, first.cursor, first.profile, true);
	first.generation = from_copy.id;
	first.ordinal = from_copy.ordinal;
	auto other_cluster = first.cursor;
	other_cluster.cluster.fill('c');
	check(l::next_generation(first, other_cluster, first.profile, true).id != first.generation);
	selected.ordinal = UINT32_MAX;
	bool exhausted = false;
	try {
		l::build_ordinal(selected, selected.cursor);
	} catch (const std::overflow_error &) {
		exhausted = true;
	}
	check(exhausted);
}
// Three maintenance rebuilds of one head through DiskStore: each takes the next ordinal and a new name.
void rebuilds() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("derived-names-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{directory};
	auto heap = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
	auto mappings = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
	auto controls = std::make_shared<d::StorageLedger>(1024 * 1024);
	d::RetirementRuntime runtime(controls, 1);
	auto domain = runtime.domain(heap, 512);
	{
		auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
		l::GenerationId seed{};
		seed.fill('f');
		l::detail::PagedSnapshotBuilder builder(source, seed);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		install(directory, builder.take());
		write(directory / "owner.lock", {});
		l::DiskStoreOptions options;
		options.limits = {64 * l::paged::segment_bytes, 1000, 10};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = mappings;
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		if (store->error())
			std::rethrow_exception(store->error());
		check(store->status() == l::StoreStatus::Ready);
		auto head = *store->selection().descriptor;
		check(head.generation == seed && head.ordinal == 0);
		std::set<l::GenerationId> names{seed};
		auto base = l::recover_generation(directory, options.recovery_limits);
		for (std::uint32_t r = 1; r <= 3; ++r) {
			l::detail::PagedMaintenanceBuilder builder(base, domain.factory(), heap, options.recovery_limits);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			auto plan = builder.take();
			const auto wanted = plan.descriptor();
			check(wanted.cursor == head.cursor && wanted.ordinal == r);
			check(wanted.generation == l::next_generation(head, head.cursor, head.profile, true).id);
			check(names.insert(wanted.generation).second);
			store->begin(std::move(plan));
			drive(store);
			if (store->publication_error())
				std::rethrow_exception(store->publication_error());
			check(store->publication_status() == l::DiskPublicationStatus::Published);
			check(store->selection().descriptor == wanted && l::read_generation_selection(directory).descriptor == wanted);
			check(std::filesystem::exists(directory / (l::generation_name(wanted.generation) + ".lease")));
			head = wanted;
			auto recovered =
				l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store));
			check(recovered.descriptor() == head);
			store->confirm_recovered(recovered);
			drive(store);
			base = std::move(recovered);
		}
		check(head.ordinal == 3 && names.size() == 4);
		store->close();
		drive(store);
	}
	domain.close().wait();
	check(mappings->used() == 0);
}
int main() {
	ordinals();
	rebuilds();
	std::cout << "derived names: rebuilds at one head take ordinals 1, 2, 3 under distinct names\n";
}
