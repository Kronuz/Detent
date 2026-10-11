#include "paged_dependency_discovery.hh"
#include "paged_graph_validator.hh"
#include "paged_large_fixture.hh"
#include "paged_snapshot_builder.hh"
#include "snapshot_fixture.hh"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
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
void recover_dependencies(const l::detail::PagedImage &image, std::shared_ptr<d::StorageLedger> account,
						  l::StorageIdentity storage, std::size_t quantum, bool corrupt = false) {
	auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-discovery-" + std::to_string(::getpid()));
	std::filesystem::create_directories(directory);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto controls = std::make_shared<d::StorageLedger>(4 * p::segment_bytes);
	d::RetirementRuntime runtime{controls, 1};
	auto domain = runtime.domain(account, image.segments().size() + 4);
	const auto baseline = account->used();
	{
		std::ofstream junk(directory / "unrelated");
		junk << "not a segment";
	}
	unsigned resolved = 0;
	{
		l::detail::DependencyDiscovery discovery(account, domain, image.root(), storage);
		while (!discovery.done()) {
			l::ValidationBudget budget{quantum, 16};
			discovery.step(budget);
			if (auto missing = discovery.missing()) {
				const auto found =
					std::find_if(image.segments().begin(), image.segments().end(),
								 [&](const auto &s) { return s.ownership.segment == *missing; });
				require(found != image.segments().end());
				auto path = directory / ("segment-" + std::to_string(resolved++));
				{
					std::ofstream out(path, std::ios::binary);
					if (corrupt && resolved == 1) {
						out.write(found->bytes->data(), found->bytes->size() - 1);
						out.put(found->bytes->data()[found->bytes->size() - 1] ^ 1);
					} else
						out.write(found->bytes->data(), found->bytes->size());
					require(bool(out));
				}
				require(::chmod(path.c_str(), 0600) == 0);
				auto fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
				require(fd >= 0);
				auto mapping =
					domain.make<l::detail::SegmentMapping>(account, fd, ::getuid(), found->ownership);
				require(::close(fd) == 0);
				discovery.supply(std::move(mapping));
			}
		}
		auto registry = discovery.take();
		require(resolved == image.segments().size() && registry->inputs().size() == resolved);
		auto lease = std::allocate_shared<d::RetirementHandle<l::detail::SegmentRegistry>>(
			d::StorageAllocator<d::RetirementHandle<l::detail::SegmentRegistry>>(account), registry);
		l::detail::GraphValidator validator(account, lease, registry->inputs(), image.root(), storage);
		while (!validator.done()) {
			l::ValidationBudget budget{quantum, 16};
			validator.step(budget);
		}
		std::uint64_t reachable_pages = 0, reachable_bytes = 0;
		for (std::size_t i = 0; i < registry->inputs().size(); ++i) {
			const auto reachable = validator.segment_reachability(i);
			require(reachable.pages && reachable.bytes <= registry->inputs()[i].bytes.size());
			reachable_pages += reachable.pages;
			reachable_bytes += reachable.bytes;
		}
		require(reachable_pages == image.root().pages && reachable_bytes == image.root().bytes);
		rejects<std::out_of_range>([&] { validator.segment_reachability(registry->inputs().size()); });
		rejects<std::logic_error>([&] { registry->validated_root(); });
		for (;;) {
			l::ValidationBudget proof{1, 1};
			if (validator.record_reachability(*registry, proof))
				break;
		}
		require(registry->validated_root().logical == image.root().logical);
		for (std::size_t i = 0; i < registry->inputs().size(); ++i)
			require(registry->reachability(i).pages == validator.segment_reachability(i).pages);
		auto view = validator.take(domain);
		require(view.cursor() == image.root().cursor && view.size() == image.root().records);
		require(view.get("app", image.root().records == 2 ? "count" : "k000000").value() ==
				(image.root().records == 2 ? INT64_MIN : 0));
		for (;;) {
			l::ValidationBudget budget{0, 1};
			if (discovery.discard(budget))
				break;
		}
		std::filesystem::remove_all(directory);
		require(view.cursor() == image.root().cursor && view.size() == image.root().records);
		require(view.get("app", image.root().records == 2 ? "count" : "k000000").value() ==
				(image.root().records == 2 ? INT64_MIN : 0));
	}
	domain.close().wait();
	require(account->used() == baseline);
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
		recover_dependencies(image, account, storage, 1);
		{
			auto controls = std::make_shared<d::StorageLedger>(4 * p::segment_bytes);
			d::RetirementRuntime runtime{controls, 1};
			auto domain = runtime.domain(account, 4);
			auto empty = image.root();
			empty.bytes = empty.pages = empty.segments = empty.records = empty.namespaces = 0;
			empty.tree = {};
			empty.logical = p::empty_commitment();
			l::detail::DependencyDiscovery discovery(account, domain, empty, storage);
			require(!discovery.missing());
			l::ValidationBudget work{1, 1};
			discovery.step(work);
			require(discovery.done());
			auto registry = discovery.take();
			require(registry->inputs().empty() && registry->physical_bytes() == 0);
			auto pressure = account->admit(account->limit() - account->used());
			rejects<d::StoragePressure>(
				[&] { l::detail::DependencyDiscovery rejected(account, domain, image.root(), storage); });
			pressure.reset();
			l::ValidationBudget none{0, 0};
			require(!discovery.discard(none));
			l::ValidationBudget disposal{0, 1};
			require(discovery.discard(disposal));
			registry.reset();
			domain.close().wait();
		}

		rejects<std::invalid_argument>([&] { recover_dependencies(image, account, storage, 1, true); });
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
		auto controls = std::make_shared<d::StorageLedger>(4 * p::segment_bytes);
		d::RetirementRuntime runtime{controls, 1};
		auto domain = runtime.domain(account, 1);
		l::detail::PagedSnapshotBuilder builder(source, storage, generation);
		build(builder, 8192);
		auto image = builder.take();
		auto owner = domain.make<l::detail::PagedImageOwner>(std::move(image));
		require(owner->image().root().records == 2);
		rejects<d::StoragePressure>([&] {
			l::detail::PagedSnapshotBuilder second(source, storage, generation);
			build(second, 8192);
			auto pending = second.take();
			try {
				auto rejected = domain.make<l::detail::PagedImageOwner>(std::move(pending));
			} catch (...) {
				require(pending.root().records == 2);
				throw;
			}
		});
		auto pinned = owner;
		owner.reset();
		auto drain = domain.close();
		require(!drain.done() && pinned->image().root().records == 2);
		pinned.reset();
		drain.wait();
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
		recover_dependencies(image, ledger, storage, 8192);
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
