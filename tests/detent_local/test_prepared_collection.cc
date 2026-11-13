#include "detent_local/disk_store.hh"
#include "detent_local/reader.hh"
#include "managed_recovery.hh"
#include "segment_registry.hh"
#include "paged_compaction_plan.hh"
#include "prepared_reader_fixture.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <sys/file.h>
#include <thread>
namespace d = detent::distribution;
namespace l = detent::local;
namespace x = l::detail;
using prepared_test::check;
namespace {
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
void wait_unpinned(const std::filesystem::path &path) {
	int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
	check(fd >= 0);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
		check(errno == EWOULDBLOCK || errno == EAGAIN);
		check(std::chrono::steady_clock::now() < deadline);
		std::this_thread::yield();
	}
	check(::close(fd) == 0);
}
} // namespace
int main() {
	auto fixture = std::make_shared<prepared_test::Fixture>();
	prepared_test::write_file(fixture->path / "owner.lock", {});
	auto heap = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(heap, 256);
	l::DiskStoreOptions options;
	options.limits = {64 * l::paged::segment_bytes, 100, 10};
	options.recovery_limits.retirement_domain = &domain;
	options.recovery_limits.mapping_account =
	    std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	{
		l::GenerationReader reader(fixture->path, options.recovery_limits, {}, ::getuid());
        check(reader.refresh());
        auto old_view = reader.read_view();
		auto store = l::make_disk_store(domain, fixture->path, std::make_shared<int>(1), options);
		drive(store);
		const auto old_pack = fixture->definitions[1].segment;
		const x::SegmentNames old_names(old_pack);
		{
			auto base = l::recover_generation(fixture->path, options.recovery_limits);
			l::GenerationId next{};
			next.fill('z');
			x::PagedCompactionBuilder compaction(base, {&old_pack, 1}, domain);
			while (!compaction.done()) {
				l::ValidationBudget budget;
				compaction.step(budget);
			}
			auto plan = compaction.take();
			check(plan.maintenance() && l::paged::state_commitment(plan.root()) == l::paged::state_commitment(fixture->root));
			bool old_pack_removed = false;
			for (std::size_t i = 0; i != plan.registry()->inputs().size(); ++i)
				if (plan.registry()->inputs()[i].ownership.segment == old_pack)
					old_pack_removed = plan.base_reachability(i).pages == 0;
			check(old_pack_removed);
			store->begin(std::move(plan));
			drive(store);
			check(store->publication_status() == l::DiskPublicationStatus::Published);
			auto current = x::ManagedRecovery::recover(fixture->path, x::ManagedRecovery::capture(*store));
			store->confirm_recovered(current);
			check(current.view().get("a", "k").value() == 42);
		}
        check(reader.refresh());
        store->request_collection();
        drive(store);
        check(std::filesystem::exists(fixture->path / old_names.payload_name()));
		check(std::filesystem::exists(fixture->path / (l::generation_name(fixture->generation) + ".lease")));
		// The prepared root opens its retired pack only after collection runs.
		check(old_view.get("b", "k").value() == 1);
		old_view = {};
		wait_unpinned(fixture->path / (l::generation_name(fixture->generation) + ".lease"));
		store->request_collection();
		drive(store);
		check(!std::filesystem::exists(fixture->path / old_names.payload_name()));
		check(!std::filesystem::exists(fixture->path / old_names.owner_name()));
		check(!std::filesystem::exists(fixture->path / (l::generation_name(fixture->generation) + ".read")));
        check(reader.read_view().get("a", "k").value() == 42);
        check(reader.read_view().get("b", "k").value() == 1);
		store->close();
		drive(store);
		check(store->drained());
	}
	domain.close().wait();
}
