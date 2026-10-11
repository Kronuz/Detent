#include "paged_graph_validator.hh"
#include "paged_snapshot_builder.hh"
#include "snapshot_fixture.hh"
#include <algorithm>
#include <cstring>
#include <vector>

namespace l = detent::local;
namespace p = l::paged;
namespace {
template <class Error, class F> void rejects(F call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid paged snapshot operation accepted");
}
void build(l::detail::PagedSnapshotBuilder &builder, std::size_t bytes = 1) {
	unsigned turns = 0;
	while (!builder.done()) {
		l::ValidationBudget budget{bytes, 16};
		builder.step(budget);
		require(budget.bytes <= bytes && budget.rows <= 16 && ++turns < 5000000);
	}
}
struct Lease {
	l::detail::PagedImage image;
	std::vector<l::detail::SegmentInput, d::StorageAllocator<l::detail::SegmentInput>> segments;
	Lease(l::detail::PagedImage value, std::shared_ptr<d::StorageLedger> account)
		: image(std::move(value)),
		  segments(d::StorageAllocator<l::detail::SegmentInput>(std::move(account))) {
		segments.reserve(image.segments().size());
		for (const auto &segment : image.segments())
			segments.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
		std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
		});
	}
};
std::string many(unsigned count) {
	std::string out;
	auto a = [&](std::uint32_t size) { m::array(out, size); };
	auto n = [&](std::uint64_t value) { m::unsigned_integer(out, value); };
	auto s = [&](std::string_view value) { m::string(out, value); };
	a(7);
	n(1);
	n(1);
	a(2);
	n(1);
	n(1);
	a(5);
	const auto id = cursor();
	for (const auto *field : {&id.cluster, &id.group, &id.restore_epoch})
		m::string(out, {field->data(), field->size()}, true);
	m::string(out, {id.scope.data(), id.scope.size()}, true);
	n(id.revision);
	a(4);
	n(1);
	n(count);
	n(0);
	n(3 + 11ull * count);
	a(1);
	a(2);
	s("app");
	n(42);
	a(count);
	for (unsigned i = 0; i < count; ++i) {
		const auto digits = std::to_string(i);
		const auto name = "app:k" + std::string(6 - digits.size(), '0') + digits;
		a(4);
		n(0);
		s(name);
		n(1);
		a(2);
		n(1);
		m::signed_integer(out, i);
	}
	return out;
}
std::shared_ptr<const d::ValidatedSnapshot> charged(std::shared_ptr<d::StorageLedger> account) {
	d::Limits limits;
	limits.storage_account = account;
	d::SnapshotDecoder decoder(cursor(), {"app"}, limits);
	const auto wire = fixture();
	auto input = std::span<const char>(wire);
	while (!input.empty()) {
		d::WorkBudget budget;
		decoder.feed(input, budget);
	}
	return std::allocate_shared<const d::ValidatedSnapshot>(
		d::StorageAllocator<const d::ValidatedSnapshot>(account), decoder.finish());
}
} // namespace
int main() {
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	auto account = source->storage_account();
	const auto baseline = account->used();
	p::Digest logical;
	{
		l::detail::PagedSnapshotBuilder builder(source, storage, generation);
		rejects<std::logic_error>([&] { builder.take(); });
		build(builder);
		auto image = builder.take();
		require(!builder.done() && image.segments().size() == 1 && image.root().records == 2);
		logical = image.root().logical;
		require(image.descriptor().generation == generation && image.descriptor().cursor == cursor());
		require(image.descriptor().checksum ==
				p::physical_commitment({image.bytes()->data(), image.bytes()->size()}));
		require(p::decode_root({image.bytes()->data(), image.bytes()->size()}).logical == logical);
		auto lease =
			std::allocate_shared<Lease>(d::StorageAllocator<Lease>(account), std::move(image), account);
		l::detail::GraphValidator validator(account, lease, lease->segments, lease->image.root(), storage);
		while (!validator.done()) {
			l::ValidationBudget budget{1, 1};
			validator.step(budget);
		}
		auto controls = std::make_shared<d::StorageLedger>(4 * p::segment_bytes);
		d::RetirementRuntime runtime{controls, 1};
		auto domain = runtime.domain(controls, 16);
		auto view = validator.take(domain);
		lease.reset();
		require(view.get("app", "count").value() == INT64_MIN && view.eval("app", "flag").value());
		require(view.eval("app", "flag", "host", "beta").value() ==
				(detent::stable_bucket("app:flag", "host") < 17));
		view = {};
		domain.close().wait();
	}
	require(account->used() == baseline);
	{
		l::detail::PagedSnapshotBuilder builder(source, storage, generation);
		l::ValidationBudget work{8192, 1};
		builder.step(work);
		require(!builder.done());
		l::ValidationBudget empty{0, 0};
		require(!builder.discard(empty));
		l::ValidationBudget first{0, 1};
		require(!builder.discard(first));
		rejects<std::logic_error>([&] { builder.step(first); });
		for (;;) {
			l::ValidationBudget budget{0, 1};
			if (builder.discard(budget))
				break;
		}
	}
	require(account->used() == baseline);
	{
		auto ledger = std::make_shared<d::StorageLedger>(8 * p::segment_bytes);
		auto input = charged(ledger);
		const auto retained = ledger->used();
		std::size_t peak;
		{
			l::detail::PagedSnapshotBuilder builder(input, storage, generation);
			build(builder);
			peak = ledger->peak() - retained;
			auto image = builder.take();
			for (;;) {
				l::ValidationBudget budget{0, 1};
				if (image.discard(budget))
					break;
			}
		}
		require(ledger->used() == retained);
		auto pressure = ledger->admit(ledger->limit() - retained - peak + 1);
		const auto occupied = ledger->used();
		{
			l::detail::PagedSnapshotBuilder builder(input, storage, generation);
			rejects<d::StoragePressure>([&] { build(builder); });
			rejects<std::logic_error>([&] { builder.take(); });
			for (;;) {
				l::ValidationBudget budget{0, 1};
				if (builder.discard(budget))
					break;
			}
		}
		require(ledger->used() == occupied);
		pressure.reset();
		require(ledger->used() == retained);
	}
	{
		generation.fill('h');
		l::detail::PagedSnapshotBuilder builder(source, storage, generation);
		build(builder);
		auto image = builder.take();
		require(image.root().logical == logical);
		l::ValidationBudget empty{0, 0};
		require(!image.discard(empty));
		for (;;) {
			l::ValidationBudget budget{0, 1};
			if (image.discard(budget))
				break;
		}
		rejects<std::logic_error>([&] { image.root(); });
	}
	require(account->used() == baseline);
	{
		p::Limits limits;
		limits.prefix_bytes = 0;
		l::detail::PagedSnapshotBuilder builder(source, storage, generation, limits);
		rejects<std::invalid_argument>([&] { build(builder); });
		for (;;) {
			l::ValidationBudget budget{0, 1};
			if (builder.discard(budget))
				break;
		}
	}
	require(account->used() == baseline);
	{
		auto large = std::make_shared<const d::ValidatedSnapshot>(decode(many(20000)));
		auto ledger = large->storage_account();
		const auto retained = ledger->used();
		l::detail::PagedSnapshotBuilder builder(large, storage, generation);
		build(builder, 8192);
		auto image = builder.take();
		require(image.segments().size() > 1 && image.root().records == 20000);
		auto lease =
			std::allocate_shared<Lease>(d::StorageAllocator<Lease>(ledger), std::move(image), ledger);
		l::detail::GraphValidator validator(ledger, lease, lease->segments, lease->image.root(), storage);
		while (!validator.done()) {
			l::ValidationBudget budget;
			validator.step(budget);
		}
		require(validator.root().records == 20000);
		l::ValidationBudget disposal{0, 1};
		require(validator.discard(disposal));
		lease.reset();
		require(ledger->used() == retained);
	}
	std::cout << "validated snapshot to paged segments/root/view, rollover and quota rejection passed\n";
}
