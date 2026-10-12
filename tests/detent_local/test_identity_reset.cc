#include "detent_distribution/messagepack.hh"
#include "detent_local/disk_store.hh"
#include <completion/posix.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <unistd.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace c = kronuz::io::completion;
namespace {
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("trusted reset invariant at line " + std::to_string(location.line()));
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	while (store->operation() || store->needs_work()) {
		auto job = store->operation();
		if (!job) {
			store->advance();
			continue;
		}
		job->submitted();
		check(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
		store->advance();
	}
}
std::unique_ptr<l::ImageBuilder> build(const d::Cursor &cursor, char generation,
									   std::shared_ptr<d::StorageLedger> account) {
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
	limits.storage_account = std::move(account);
	d::SnapshotDecoder decoder(cursor, {}, limits);
	auto input = std::span<const char>(wire);
	while (!input.empty()) {
		d::WorkBudget budget;
		decoder.feed(input, budget);
	}
	auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
	l::GenerationId id;
	id.fill(generation);
	auto builder = std::make_unique<l::ImageBuilder>(snapshot, id);
	while (!builder->done()) {
		l::ValidationBudget budget;
		builder->step(budget);
	}
	return builder;
}
std::string read(const std::filesystem::path &path) {
	std::ifstream stream(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
} // namespace
int main() {
	try {
		auto directory =
			std::filesystem::current_path() / ".scratch" / ("identity-reset-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
		d::RetirementRuntime runtime(account, 2);
		auto domain = runtime.domain(account, 1);
		l::DiskStoreOptions options;
		options.mode = l::StoreOpenMode::FreshEmpty;
		options.limits = {1024 * 1024, 100, 10};
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		d::Cursor old;
		old.cluster.fill('c');
		old.group.fill('g');
		old.restore_epoch.fill('e');
		old.scope.fill('s');
		old.revision = 42;
		auto first = build(old, 'a', account);
		store->begin(*first);
		drive(store);
		auto actual = *store->selection().descriptor;
		auto metadata = read(directory / "storage.meta");
		auto target = old;
		target.restore_epoch.fill('r');
		target.revision = 1;
		auto candidate = build(target, 'b', account);
		l::TrustedIdentityReset reset{actual, target, {}};
		for (unsigned fault = 0; fault != 3; ++fault) {
			auto wrong = reset;
			if (fault == 0)
				++wrong.expected_selected.length;
			if (fault == 1)
				wrong.configured.scope[0] ^= 1;
			if (fault == 2)
				wrong.profile.semantic_id = 2;
			bool rejected = false;
			try {
				store->begin_reset(*candidate, wrong);
			} catch (const std::invalid_argument &) {
				rejected = true;
			}
			check(rejected && store->selection().descriptor == actual && store->publication_ready());
		}
		for (auto revision : {41u, 42u, 43u}) {
			auto identity = old;
			identity.revision = revision;
			auto same = build(identity, 'd', account);
			bool rejected = false;
			try {
				store->begin_reset(*same, {actual, old, {}});
			} catch (const std::invalid_argument &) {
				rejected = true;
			}
			check(rejected);
		}
		store->begin_reset(*candidate, reset);
		drive(store);
		check(store->selection().descriptor == candidate->descriptor());
		check(store->publication_status() == l::DiskPublicationStatus::Published);
		check(read(directory / "storage.meta") == metadata);
		check(l::recover_generation(directory).descriptor() == candidate->descriptor());
		auto next = target;
		next.restore_epoch.fill('x');
		next.revision = 2;
		auto cancelled = build(next, 'z', account);
		store->begin_reset(*cancelled, {candidate->descriptor(), next, {}});
		bool replaced = false;
		while (store->operation() || store->needs_work()) {
			auto job = store->operation();
			if (!job) {
				store->advance();
				continue;
			}
			auto kind = job->request().kind;
			job->submitted();
			check(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
			if (kind == c::Kind::Replace) {
				replaced = true;
				store->cancel_publication();
			}
			store->advance();
		}
		// Published describes durable selection, not caller acknowledgement.
		check(replaced && store->publication_status() == l::DiskPublicationStatus::Published);
		check(store->selection().knowledge == l::SelectionKnowledge::Known &&
			  store->selection().descriptor == cancelled->descriptor());
		check(read(directory / "storage.meta") == metadata);
		auto directory_identity = store->directory_identity();
		store->close();
		drive(store);
		check(store->directory_identity() == directory_identity);
		store.reset();
		first.reset();
		candidate.reset();
		auto selected = cancelled->descriptor();
		cancelled.reset();
		domain.close().wait();
		auto recovery_domain = runtime.domain(account, 1);
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
		std::cout << "trusted identity reset, preflight rejection and unchanged storage UUID passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
