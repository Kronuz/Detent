#include "applied_delta_fixture.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include "paged_navigation.hh"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
namespace p = l::paged;
template <class Error, class F> void rejects(F call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid delta publication plan accepted");
}
std::string heavy(bool snapshot) {
	constexpr unsigned count = 256, overrides = 256;
	std::string wire;
	auto a = [&](unsigned n) { m::array(wire, n); };
	auto n = [&](std::uint64_t v) { m::unsigned_integer(wire, v); };
	auto text = [&](std::string_view v) { m::string(wire, v); };
	auto boolean = [&] {
		a(4);
		n(0);
		wire.push_back('\xc2');
		wire.push_back('\xc0');
		a(overrides);
		for (unsigned i = 0; i < overrides; ++i) {
			const auto digits = std::to_string(i);
			a(3);
			text("g" + std::string(6 - digits.size(), '0') + digits + std::string(25, 'x'));
			n(0);
			wire.push_back('\xc3');
		}
	};
	a(7);
	n(1);
	n(snapshot ? 1 : 2);
	a(2);
	n(1);
	n(1);
	auto target = cursor();
	target.revision = 43;
	if (!snapshot)
		put_cursor(wire, cursor());
	put_cursor(wire, target);
	a(4);
	n(1);
	n(count + (snapshot ? 2 : 0));
	n(count * overrides + (snapshot ? 1 : 0));
	n(count * (11 + overrides * 32ull) + (snapshot ? 24 : 3));
	a(1);
	if (!snapshot) {
		a(3);
		n(43);
		a(1);
	}
	a(2);
	text("app");
	n(43);
	a(count + (snapshot ? 2 : 0));
	if (snapshot) {
		a(4);
		n(0);
		text("app:count");
		n(41);
		a(2);
		n(1);
		m::signed_integer(wire, INT64_MIN);
		a(4);
		n(0);
		text("app:flag");
		n(42);
		a(4);
		n(0);
		wire.push_back('\xc3');
		n(25);
		a(1);
		a(3);
		text("beta");
		n(1);
		n(17);
	}
	for (unsigned i = 0; i < count; ++i) {
		const auto digits = std::to_string(i);
		a(4);
		n(0);
		text("app:n" + std::string(6 - digits.size(), '0') + digits);
		n(snapshot ? 43 : 0);
		boolean();
	}
	return wire;
}
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-delta-plan-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{}, next{};
	storage.fill('s');
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder original(snapshot, storage, generation);
	while (!original.done()) {
		l::ValidationBudget work;
		original.step(work);
	}
	auto image = original.take();
	install(directory, image, storage);
	auto recovered = l::recover_generation(directory);
	for (unsigned transactions : {1u, 2u, 17u}) {
		auto account = std::make_shared<d::StorageLedger>(16 * p::segment_bytes);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 8);
		const auto baseline = account->used();
		{
			auto input = parse_delta(false, transactions, "app", account);
			l::detail::PagedDeltaPlanBuilder builder(recovered, std::move(input), next, domain);
			require(input.storage_capacity() == 0);
			rejects<std::logic_error>([&] { builder.take(); });
			l::ValidationBudget none{0, 0};
			builder.step(none);
			require(!builder.done());
			for (unsigned turn = 0; !builder.done(); ++turn) {
				require(turn < 2000000);
				l::ValidationBudget work{1, 1};
				builder.step(work);
			}
			auto plan = builder.take();
			rejects<std::logic_error>([&] { builder.take(); });
			require(plan.base_descriptor() == recovered.descriptor() &&
					plan.directory_identity() == recovered.directory_identity() &&
					plan.storage_identity() == storage);
			require(plan.descriptor().generation == next &&
					plan.descriptor().cursor.revision == 43 + transactions);
			require(plan.segments().size() == 1 && plan.base_reachability(0).pages == 0);
			require(plan.new_reachability(0).pages == plan.root().pages &&
					plan.override_histogram()[0] == plan.root().records);
			require(plan.counts().ints == (transactions == 1 ? 0 : 1) && plan.counts().bools == 1);
			l::detail::PagedSnapshotBuilder oracle(expected(false, transactions), storage, next);
			while (!oracle.done()) {
				l::ValidationBudget work;
				oracle.step(work);
			}
			auto wanted = oracle.take();
			require(plan.root().logical == wanted.root().logical);
			require(plan.descriptor().checksum ==
					p::physical_commitment({plan.bytes()->data(), plan.bytes()->size()}));
		}
		domain.close().wait();
		require(account->used() == baseline);
	}
	for (unsigned invalid = 0; invalid < 3; ++invalid) {
		auto account = std::make_shared<d::StorageLedger>(16 * p::segment_bytes);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 8);
		const auto baseline = account->used();
		{
			auto input = parse_delta(invalid == 0, 2, invalid == 2 ? "bad" : "app", account);
			rejects<std::invalid_argument>([&] {
				l::detail::PagedDeltaPlanBuilder builder(recovered, std::move(input),
														 invalid == 1 ? generation : next, domain);
				while (!builder.done()) {
					l::ValidationBudget work;
					builder.step(work);
				}
			});
		}
		domain.close().wait();
		require(account->used() == baseline);
	}
	{
		auto account = std::make_shared<d::StorageLedger>(16 * p::segment_bytes);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 8);
		const auto baseline = account->used();
		{
			auto input = parse_delta(false, 17, "app", account);
			l::detail::PagedDeltaPlanBuilder builder(recovered, std::move(input), next, domain);
			auto pressure = account->admit(account->limit() - account->used());
			rejects<d::StoragePressure>([&] {
				while (!builder.done()) {
					l::ValidationBudget work;
					builder.step(work);
				}
			});
			pressure.reset();
			l::ValidationBudget work;
			rejects<std::logic_error>([&] { builder.step(work); });
			require(builder.discard(work));
		}
		domain.close().wait();
		require(account->used() == baseline);
	}
	for (unsigned partial : {0u, 2500u, 15000u, 1000000u}) {
		auto account = std::make_shared<d::StorageLedger>(16 * p::segment_bytes);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 8);
		const auto baseline = account->used();
		{
			auto input = parse_delta(false, 17, "app", account);
			l::detail::PagedDeltaPlanBuilder builder(recovered, std::move(input), next, domain);
			for (unsigned turn = 0; turn < partial && !builder.done(); ++turn) {
				l::ValidationBudget work{1, 1};
				builder.step(work);
			}
			l::ValidationBudget none{0, 0};
			require(!builder.discard(none));
			l::ValidationBudget disposal{0, 1};
			require(builder.discard(disposal));
			rejects<std::logic_error>([&] { builder.step(disposal); });
		}
		domain.close().wait();
		require(account->used() == baseline);
	}
	{
		auto account = std::make_shared<d::StorageLedger>(16 * p::segment_bytes);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 2);
		const auto baseline = account->used();
		{
			auto input = parse_delta(false, 2, "app", account);
			rejects<d::StoragePressure>(
				[&] { l::detail::PagedDeltaPlanBuilder builder(recovered, std::move(input), next, domain); });
		}
		domain.close().wait();
		require(account->used() == baseline);
	}
	{
		auto account = std::make_shared<d::StorageLedger>(32 * p::segment_bytes);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 8);
		const auto baseline = account->used();
		{
			d::Limits limits;
			limits.storage_account = account;
			d::DeltaDecoder decoder(cursor(), {"app"}, limits);
			const auto wire = heavy(false);
			auto input = std::span<const char>(wire);
			while (!input.empty()) {
				d::WorkBudget work;
				decoder.feed(input, work);
			}
			l::detail::PagedDeltaPlanBuilder builder(recovered, decoder.finish(), next, domain);
			for (unsigned turn = 0; !builder.done(); ++turn) {
				require(turn < 2000000);
				l::ValidationBudget work{4096, 16};
				builder.step(work);
			}
			auto plan = std::make_shared<l::detail::PagedPlan>(builder.take());
			require(plan->segments().size() >= 3 && plan->base_reachability(0).pages == 2);
			require(plan->counts().ints == 1 && plan->counts().bools == 257 &&
					plan->counts().overrides == 65537 && plan->counts().strings == 2099992);
			require(plan->override_histogram()[0] == 1 && plan->override_histogram()[1] == 1 &&
					plan->override_histogram()[256] == 256);
			auto target = cursor();
			target.revision = 43;
			auto expected = std::make_shared<const d::ValidatedSnapshot>(decode(heavy(true), target));
			l::detail::PagedSnapshotBuilder oracle(expected, storage, next);
			while (!oracle.done()) {
				l::ValidationBudget work;
				oracle.step(work);
			}
			auto wanted = oracle.take();
			require(plan->root().logical == wanted.root().logical);
			std::vector<l::detail::SegmentInput> merged;
			for (const auto &segment : image.segments())
				merged.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
			for (const auto &segment : plan->segments())
				merged.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
			std::sort(merged.begin(), merged.end(), [](const auto &a, const auto &b) {
				return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
			});
			l::detail::GraphValidator graph(account, plan, merged, plan->root(), storage);
			while (!graph.done()) {
				l::ValidationBudget work;
				graph.step(work);
			}
			for (std::size_t i = 0; i < plan->segments().size(); ++i) {
				const auto index = l::detail::find_segment(merged, plan->segments()[i].ownership.segment);
				const auto live = graph.segment_reachability(index);
				require(live.pages == plan->new_reachability(i).pages &&
						live.bytes == plan->new_reachability(i).bytes);
			}
		}
		domain.close().wait();
		require(account->used() == baseline);
	}
	{
		const auto detached = directory / "detached";
		install(detached, image, storage);
		auto base = std::optional<l::RecoveredGeneration>(l::recover_generation(detached));
		auto account = std::make_shared<d::StorageLedger>(16 * p::segment_bytes);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 8);
		const auto baseline = account->used();
		{
			auto input = parse_delta(false, 2, "app", account);
			l::detail::PagedDeltaPlanBuilder builder(*base, std::move(input), next, domain);
			base.reset();
			while (!builder.done()) {
				l::ValidationBudget work;
				builder.step(work);
			}
			auto plan = builder.take();
			auto moved = std::move(plan);
			rejects<std::logic_error>([&] { plan.root(); });
			const int lease =
				::open((detached / (l::generation_name(generation) + ".lease")).c_str(), O_RDWR);
			require(lease >= 0);
			require(::flock(lease, LOCK_EX | LOCK_NB) == -1);
			::close(lease);
			require(moved.base_descriptor() == image.descriptor());
		}
		domain.close().wait();
		require(account->used() == baseline);
	}
	std::cout << "exact-base sparse plans, transferred input, cancellation and owner pressure passed\n";
}
