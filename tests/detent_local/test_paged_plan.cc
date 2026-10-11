#include "paged_disk_fixture.hh"
#include "paged_plan.hh"
#include <fcntl.h>
#include <sys/file.h>
#include <thread>
namespace p = l::paged;
template <class Error, class F> void rejects(F action) {
	try {
		action();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid progress plan accepted");
}
std::shared_ptr<const d::ValidatedDelta> progress(d::Cursor base, std::shared_ptr<d::StorageLedger> account) {
	std::string out;
	m::array(out, 5);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 3);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	auto encode = [&](const d::Cursor &c) {
		m::array(out, 5);
		for (const auto *field : {&c.cluster, &c.group, &c.restore_epoch})
			m::string(out, {field->data(), field->size()}, true);
		m::string(out, {c.scope.data(), c.scope.size()}, true);
		m::unsigned_integer(out, c.revision);
	};
	encode(base);
	auto target = base;
	target.revision += 3;
	encode(target);
	d::Limits limits;
	limits.storage_account = account;
	d::DeltaDecoder decoder(base, {"app"}, limits, d::UpdateKind::Progress);
	auto input = std::span<const char>(out.data(), out.size());
	while (!input.empty()) {
		d::WorkBudget b{1, 1};
		decoder.feed(input, b);
	}
	return std::make_shared<const d::ValidatedDelta>(decoder.finish());
}
int main() {
	auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-plan-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code e;
			std::filesystem::remove_all(path, e);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	auto account = source->storage_account();
	l::StorageIdentity storage{}, generation{}, next{};
	storage.fill('s');
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder snapshot(source, storage, generation);
	while (!snapshot.done()) {
		l::ValidationBudget b;
		snapshot.step(b);
	}
	auto image = snapshot.take();
	install(directory, image, storage);
	auto recovered = std::optional<l::RecoveredGeneration>(l::recover_generation(directory));
	const auto base = recovered->descriptor();
	const auto identity = recovered->directory_identity();
	auto delta = progress(base.cursor, account);
	const auto baseline = account->used();
	{
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 4);
		{
			rejects<std::invalid_argument>(
				[&] { l::detail::PagedPlanBuilder invalid(*recovered, delta, generation, domain); });
			auto stale_cursor = base.cursor;
			--stale_cursor.revision;
			auto stale = progress(stale_cursor, account);
			rejects<std::invalid_argument>(
				[&] { l::detail::PagedPlanBuilder invalid(*recovered, stale, next, domain); });
			l::detail::PagedPlanBuilder cancelled(*recovered, delta, next, domain);
			l::ValidationBudget partial{7, 1};
			cancelled.step(partial);
			l::ValidationBudget no_work{0, 0};
			require(!cancelled.discard(no_work));
			l::ValidationBudget disposal{0, 1};
			require(cancelled.discard(disposal));
			rejects<std::logic_error>([&] { cancelled.step(disposal); });
		}
		{
			l::detail::PagedPlanBuilder builder(*recovered, delta, next, domain);
			rejects<std::logic_error>([&] { builder.take(); });
			l::ValidationBudget zero{0, 0};
			builder.step(zero);
			require(!builder.done());
			unsigned turns = 0;
			while (!builder.done()) {
				l::ValidationBudget b{1, 1};
				builder.step(b);
				require(++turns <= 641 && b.bytes <= 1 && b.rows <= 1);
			}
			require(turns == 640);
			auto plan = builder.take();
			rejects<std::logic_error>([&] { builder.take(); });
			require(plan.base_descriptor() == base && plan.directory_identity() == identity &&
					plan.storage_identity() == storage);
			require(plan.root().tree == image.root().tree && plan.root().logical == image.root().logical);
			require(plan.root().cursor == delta->target() && plan.descriptor().generation == next);
			require(plan.descriptor().checksum ==
					p::physical_commitment({plan.bytes()->data(), plan.bytes()->size()}));
			require(plan.registry()->validated_root().cursor == base.cursor);
			require(plan.counts().ints == 1 && plan.counts().bools == 1);
			auto moved = std::move(plan);
			rejects<std::logic_error>([&] { plan.root(); });
			const int lease =
				::open((directory / (l::generation_name(generation) + ".lease")).c_str(), O_RDWR);
			require(lease >= 0);
			recovered.reset();
			require(::flock(lease, LOCK_EX | LOCK_NB) == -1);
			::close(lease);
			std::filesystem::remove_all(directory);
			require(moved.registry()->inputs().size() == image.segments().size());
		}
		auto drain = domain.close();
		while (!drain.done())
			std::this_thread::yield();
		runtime.shutdown();
	}
	require(account->used() == baseline);
}
