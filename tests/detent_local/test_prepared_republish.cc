#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_plan.hh"
#include "prepared_reader_fixture.hh"
#include "segment_registry.hh"
#include <completion/native_completion.h>
#include <sys/file.h>
namespace d = detent::distribution;
namespace l = detent::local;
namespace x = l::detail;
using prepared_test::check;
namespace {
template <class Error, class Action> void rejects(Action action) {
	try {
		action();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid prepared republish accepted");
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	for (unsigned turns = 0;; ++turns) {
		check(turns < 100000);
		store->advance();
		auto operation = store->operation();
		if (!operation) {
			if (store->needs_work())
				continue;
			break;
		}
		operation->submitted();
		check(operation->complete(
		    kronuz::journal::detail::execute_primitive(operation->io(), operation->request())));
	}
	if (store->error())
		std::rethrow_exception(store->error());
}
} // namespace
int main() {
	auto fixture = std::make_shared<prepared_test::Fixture>();
	prepared_test::write_file(fixture->path / "owner.lock", {});
	const auto old_metadata = fixture->path / (l::generation_name(fixture->generation) + ".read");
	check(std::filesystem::remove(old_metadata));
	rejects<l::GenerationUnavailable>([&] { x::PreparedReader::open(fixture->path, ::getuid(), {}); });
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	auto heap = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(heap, 256);
	l::DiskStoreOptions options;
	options.limits = {64 * l::paged::segment_bytes, 100, 10};
	options.recovery_limits.retirement_domain = &domain;
	options.recovery_limits.mapping_account =
	    std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	{
		auto actual = l::recover_generation(fixture->path, options.recovery_limits);
		check(!x::PreparedReader::metadata_ready(fixture->path, actual, ::getuid()));
		const auto unsafe = fixture->path / "untrusted";
		std::filesystem::create_directory(unsafe);
		std::filesystem::permissions(unsafe, std::filesystem::perms::all);
		std::filesystem::create_directory_symlink(fixture->path.parent_path(), unsafe / "via");
		const auto untrusted_path = unsafe / "via" / fixture->path.filename();
		rejects<std::exception>([&] {
			x::PreparedReader::metadata_ready(untrusted_path, actual, ::getuid());
		});
		rejects<std::exception>([&] {
			x::PreparedReader::open(untrusted_path, ::getuid(), options.recovery_limits);
		});
		std::filesystem::remove_all(unsafe);
		auto metadata = x::encode_reader_metadata(x::PreparedReader::describe(actual));
		metadata[0] ^= 1;
		prepared_test::write_file(old_metadata, metadata);
		rejects<std::exception>(
		    [&] { x::PreparedReader::metadata_ready(fixture->path, actual, ::getuid()); });
		check(std::filesystem::exists(old_metadata));
		check(std::filesystem::remove(old_metadata));
		metadata = x::encode_reader_metadata(x::PreparedReader::describe(actual));
		prepared_test::write_file(old_metadata, metadata);
		check(::chmod(old_metadata.c_str(), 0622) == 0);
		rejects<std::exception>(
		    [&] { x::PreparedReader::metadata_ready(fixture->path, actual, ::getuid()); });
		check(std::filesystem::remove(old_metadata));
		auto store = l::make_disk_store(domain, fixture->path, std::make_shared<int>(1), options);
		drive(store);
		check(store->status() == l::StoreStatus::Ready);
		l::GenerationId next{};
		next.fill('r');
		rejects<std::invalid_argument>([&] {
			x::PagedPlanBuilder invalid(x::PagedPlanBuilder::Republish{}, actual, fixture->generation,
			                            domain);
		});
		rejects<std::invalid_argument>([&] {
			x::PagedPlanBuilder invalid(x::PagedPlanBuilder::Republish{}, actual, next, domain.factory(),
			                            baseline);
		});
		{
			x::PagedPlanBuilder cancelled(x::PagedPlanBuilder::Republish{}, actual, next, domain);
			l::ValidationBudget budget{7, 1};
			cancelled.step(budget);
			while (!cancelled.discard(budget))
				budget = {8192, 16};
			rejects<std::logic_error>([&] { cancelled.evidence(); });
		}
		x::PagedPlanBuilder builder(x::PagedPlanBuilder::Republish{}, actual, next, domain);
		rejects<std::logic_error>([&] { builder.evidence(); });
		unsigned turns = 0;
		while (!builder.done()) {
			l::ValidationBudget budget{1, 1};
			builder.step(budget);
			check(budget.bytes <= 1 && budget.rows <= 1 && ++turns <= 640);
		}
		check(turns == 640);
		auto evidence = builder.evidence();
		auto plan = builder.take();
		check(plan.maintenance() && plan.intent() == x::PagedPlanIntent::Maintenance);
		check(plan.descriptor().cursor == actual.descriptor().cursor &&
		      plan.root().tree == fixture->root.tree);
		check(plan.descriptor().checksum == actual.descriptor().checksum && plan.segments().empty());
		check(plan.registry()->inputs().size() == fixture->segments.size());
		check(plan.counts().bools == 1 && plan.counts().ints == 1);
		const auto wanted = plan.descriptor();
		store->begin(std::move(plan));
		drive(store);
		check(store->publication_status() == l::DiskPublicationStatus::Published);
		auto prepared = x::PreparedReader::open(fixture->path, ::getuid(), options.recovery_limits);
		check(prepared.descriptor == wanted && prepared.view.get("a", "k").value() == 42);
		auto current = x::ManagedRecovery::recover(fixture->path, x::ManagedRecovery::capture(*store));
		check(evidence->binds(actual, current));
		check(x::PreparedReader::metadata_ready(fixture->path, current, ::getuid()));
		store->confirm_recovered(current);
		check(store->publication_ready());
		for (const auto &entry : std::filesystem::directory_iterator(fixture->path))
			if (entry.path().extension() == ".pack")
				check(
				    std::any_of(fixture->segments.begin(), fixture->segments.end(), [&](const auto &segment) {
					    return entry.path().filename() ==
						       x::SegmentNames(segment.ownership.segment).payload_name();
				    }));
		store->close();
		drive(store);
		check(store->drained());
	}
	domain.close().wait();
}
