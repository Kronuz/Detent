#include "detent_distribution/messagepack.hh"
#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_snapshot_plan.hh"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <unistd.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace {
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("trusted reset invariant at line " + std::to_string(location.line()));
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
// A full copy of an empty scope at `cursor`, planned over the store's selected generation.
l::detail::PagedPlan build(const d::Cursor &cursor, const std::filesystem::path &directory,
						   const std::shared_ptr<l::DiskStore> &store, d::RetirementDomain &domain) {
	auto account = domain.storage_account();
	std::string wire;
	auto n = [&](std::uint64_t value) { d::messagepack::unsigned_integer(wire, value); };
	auto a = [&](std::uint32_t value) { d::messagepack::array(wire, value); };
	a(7);
	n(1);
	n(1);
	a(2);
	n(1);
	n(1);
	a(5);
	for (const auto *identity : {&cursor.cluster, &cursor.group, &cursor.restore_epoch})
		d::messagepack::string(wire, {identity->data(), identity->size()}, true);
	d::messagepack::string(wire, {cursor.scope.data(), cursor.scope.size()}, true);
	n(cursor.revision);
	a(4);
	n(0);
	n(0);
	n(0);
	n(0);
	a(0);
	a(0);
	d::Limits limits;
	limits.storage_account = account;
	d::SnapshotDecoder decoder(cursor, {}, limits);
	auto input = std::span<const char>(wire);
	while (!input.empty()) {
		d::WorkBudget budget;
		decoder.feed(input, budget);
	}
	auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
	std::optional<l::RecoveredGeneration> actual;
	if (store->selection().descriptor)
		actual.emplace(l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store)));
	l::detail::PagedSnapshotPlanBuilder builder(snapshot, actual, store->directory_identity(), domain.factory(),
												account, {});
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	return builder.take();
}
// Settles a paged publication: the store recovers what it selected, as the coordinator confirms it.
void confirm(const std::filesystem::path &directory, const std::shared_ptr<l::DiskStore> &store) {
	store->confirm_recovered(
		l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store)));
	drive(store);
}
} // namespace
int main() {
	try {
		auto directory =
			std::filesystem::current_path() / ".scratch" / ("identity-reset-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto account = std::make_shared<d::StorageLedger>(64ull << 20);
		d::RetirementRuntime runtime(account, 2);
		auto domain = runtime.domain(account, 64);
		l::DiskStoreOptions options;
		options.mode = l::StoreOpenMode::FreshEmpty;
		options.limits = {16 << 20, 100, 10};
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		d::Cursor old;
		old.cluster.fill('c');
		old.group.fill('g');
		old.restore_epoch.fill('e');
		old.scope.fill('s');
		old.revision = 42;
		store->begin(build(old, directory, store, domain));
		drive(store);
		confirm(directory, store);
		auto actual = *store->selection().descriptor;
		auto target = old;
		target.restore_epoch.fill('r');
		target.revision = 1;
		auto candidate = build(target, directory, store, domain);
		const auto candidate_descriptor = candidate.descriptor();
		l::TrustedIdentityReset reset{actual, d::namespace_cursor(target), {}};
		for (unsigned fault = 0; fault != 3; ++fault) {
			auto wrong = reset;
			if (fault == 0)
				++wrong.expected_selected.length;
			if (fault == 1)
				wrong.configured.incarnation[0] ^= 1;
			if (fault == 2)
				wrong.profile.semantic_id = 2;
			bool rejected = false;
			try {
				store->begin_reset(std::move(candidate), wrong);
			} catch (const std::invalid_argument &) {
				rejected = true;
			}
			check(rejected && store->selection().descriptor == actual && store->publication_ready());
		}
		for (auto revision : {41u, 42u, 43u}) {
			auto identity = old;
			identity.revision = revision;
			bool rejected = false;
			try {
				store->begin_reset(build(identity, directory, store, domain), {actual, d::namespace_cursor(old), {}});
			} catch (const std::exception &) {
				rejected = true;
			}
			check(rejected);
		}
		store->begin_reset(std::move(candidate), reset);
		drive(store);
		check(store->selection().descriptor == candidate_descriptor);
		check(store->publication_status() == l::DiskPublicationStatus::Published);
		check(l::recover_generation(directory).descriptor() == candidate_descriptor);
		confirm(directory, store);
		auto next = target;
		next.restore_epoch.fill('x');
		next.revision = 2;
		auto cancelled = build(next, directory, store, domain);
		const auto selected = cancelled.descriptor();
		store->begin_reset(std::move(cancelled), {candidate_descriptor, d::namespace_cursor(next), {}});
		// Taken work runs to its end: a cancellation after it is taken changes nothing.
		auto work = store->work();
		check(work != nullptr);
		store->cancel_publication();
		work->run();
		store->advance();
		// Published describes durable selection, not caller acknowledgement.
		check(store->publication_status() == l::DiskPublicationStatus::Published);
		check(store->selection().knowledge == l::SelectionKnowledge::Known &&
			  store->selection().descriptor == selected);
		confirm(directory, store);
		auto directory_identity = store->directory_identity();
		store->close();
		drive(store);
		check(store->directory_identity() == directory_identity);
		store.reset();
		domain.close().wait();
		auto recovery_domain = runtime.domain(account, 64);
		options.mode = l::StoreOpenMode::ExistingManaged;
		auto recovered = l::make_disk_store(recovery_domain, directory, std::make_shared<int>(1), options);
		drive(recovered);
		check(recovered->selection().knowledge == l::SelectionKnowledge::Known &&
			  recovered->selection().descriptor == selected &&
			  recovered->directory_identity() == directory_identity);
		recovered->close();
		drive(recovered);
		recovered.reset();
		recovery_domain.close().wait();
		runtime.shutdown();
		std::filesystem::remove_all(directory);
		std::cout << "trusted identity reset, preflight rejection and a reset under the same namespace passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
