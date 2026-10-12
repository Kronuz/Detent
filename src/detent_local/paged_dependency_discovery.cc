#include "paged_dependency_discovery.hh"
#include "paged_dependency_walk.hh"
#include <algorithm>
#include <cstring>
#include <sha256.h>
#include <stdexcept>
namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
namespace {
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid paged dependency graph");
}

using Handle = d::RetirementHandle<SegmentMapping>;
struct Inputs {
	std::vector<Handle, d::StorageAllocator<Handle>> mappings;
	d::RetirementHandle<SegmentRegistry> prior;
	Inputs(std::shared_ptr<d::StorageLedger> account, std::size_t capacity)
		: mappings(d::StorageAllocator<Handle>(account)) {
		mappings.reserve(capacity);
	}
};
} // namespace
struct DependencyDiscovery::Impl {
	enum class Phase { Walk, Missing, Hash, Insert, Register, Done };
	std::shared_ptr<d::StorageLedger> account;
	std::shared_ptr<Inputs> inputs;
	d::RetirementHandle<SegmentRegistry> registry;
	p::Root root;
	StorageIdentity storage;
	p::Limits limits;
	std::optional<PagedDependencyWalk> walk;
	Handle pending;
	SHA256 hash;
	std::size_t offset = 0, position = 0, moving = 0, charge = 0;
	std::uint64_t physical = 0;
	Phase phase = Phase::Walk;
	bool failed = false, discarding = false;
	Impl(std::shared_ptr<d::StorageLedger> ledger, d::RetirementFactory factory, p::Root expected,
		 StorageIdentity identity, p::Limits bounds, d::RetirementHandle<SegmentRegistry> prior,
		 d::RetirementHandle<SegmentRegistry> admitted)
		: account(std::move(ledger)), root(expected), storage(identity), limits(bounds) {
		p::encode_root(root, limits);
		require(root.segments <= p::Limits{}.segments);
		inputs = std::allocate_shared<Inputs>(d::StorageAllocator<Inputs>(account), account, root.segments);
		inputs->prior = std::move(prior);
		if (inputs->prior)
			inputs->prior->validated_root();
		auto registry_limits = limits;
		registry_limits.segments = root.segments;
		if (admitted && (!admitted->uses_account(account) || admitted->physical_bytes()))
			throw std::invalid_argument("invalid staged discovery registry");
		registry =
			admitted ? std::move(admitted) : factory.make<SegmentRegistry>(account, storage, registry_limits);
		walk.emplace(account, inputs, &lookup, root, storage, limits, inputs->prior ? &frontier : nullptr);
	}
	bool admit(std::size_t amount, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = amount;
		auto n = std::min(charge, budget.bytes);
		charge -= n;
		budget.bytes -= n;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
	static std::optional<SegmentInput> lookup(const void *owner, const StorageIdentity &id) {
		const auto &inputs = *static_cast<const Inputs *>(owner);
		auto found = std::lower_bound(
			inputs.mappings.begin(), inputs.mappings.end(), id, [](const Handle &mapping, const auto &key) {
				return std::memcmp(mapping->ownership().segment.data(), key.data(), key.size()) < 0;
			});
		if (found == inputs.mappings.end() || (*found)->ownership().segment != id)
			return {};
		return SegmentInput{(*found)->ownership(), (*found)->bytes()};
	}
	static bool frontier(const void *owner, const p::Reference &reference) {
		const auto &inputs = *static_cast<const Inputs *>(owner);
		const auto rows = inputs.prior->inputs();
		const auto found = std::lower_bound(
			rows.begin(), rows.end(), reference.segment, [](const SegmentInput &input, const auto &id) {
				return std::memcmp(input.ownership.segment.data(), id.data(), id.size()) < 0;
			});
		return found != rows.end() && found->ownership.segment == reference.segment;
	}
	auto find(const StorageIdentity &id) {
		return std::lower_bound(
			inputs->mappings.begin(), inputs->mappings.end(), id, [](const Handle &mapping, const auto &key) {
				return std::memcmp(mapping->ownership().segment.data(), key.data(), key.size()) < 0;
			});
	}
};
DependencyDiscovery::DependencyDiscovery(std::shared_ptr<d::StorageLedger> account,
										 d::RetirementDomain &domain, p::Root root, StorageIdentity identity,
										 p::Limits limits)
	: DependencyDiscovery(std::move(account), domain.factory(), root, identity, limits) {}
DependencyDiscovery::DependencyDiscovery(std::shared_ptr<d::StorageLedger> account,
										 d::RetirementFactory factory, p::Root root, StorageIdentity identity,
										 p::Limits limits, d::RetirementHandle<SegmentRegistry> prior,
										 d::RetirementHandle<SegmentRegistry> admitted)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, std::move(factory), root,
										identity, limits, std::move(prior), std::move(admitted))) {}
DependencyDiscovery::~DependencyDiscovery() = default;
DependencyDiscovery::DependencyDiscovery(DependencyDiscovery &&) noexcept = default;
void DependencyDiscovery::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed || impl_->discarding)
		throw std::logic_error("dependency discovery unavailable");
	auto &s = *impl_;
	try {
		while (s.phase != Impl::Phase::Done) {
			if (s.phase == Impl::Phase::Missing)
				return;
			if (s.phase == Impl::Phase::Hash) {
				auto bytes = s.pending->bytes();
				auto n = std::min(budget.bytes, bytes.size() - s.offset);
				if (n) {
					s.hash.add(bytes.data() + s.offset, n);
					s.offset += n;
					budget.bytes -= n;
				}
				if (s.offset != bytes.size() || !budget.rows)
					return;
				--budget.rows;
				unsigned char digest[32];
				s.hash.getHash(digest);
				require(std::memcmp(digest, s.pending->ownership().checksum.data(), 32) == 0);
				s.position = s.find(s.pending->ownership().segment) - s.inputs->mappings.begin();
				s.moving = s.inputs->mappings.size();
				s.inputs->mappings.emplace_back();
				s.phase = Impl::Phase::Insert;
			}
			if (s.phase == Impl::Phase::Insert) {
				while (s.moving > s.position) {
					if (!s.admit(sizeof(Handle), budget))
						return;
					s.inputs->mappings[s.moving] = std::move(s.inputs->mappings[s.moving - 1]);
					--s.moving;
				}
				if (!s.admit(sizeof(Handle), budget))
					return;
				s.inputs->mappings[s.position] = std::move(s.pending);
				s.phase = Impl::Phase::Walk;
			}
			if (s.phase == Impl::Phase::Register) {
				while (s.position < s.inputs->mappings.size()) {
					if (!s.admit(sizeof(SegmentInput) + sizeof(Handle), budget))
						return;
					s.registry->append(s.inputs->mappings[s.position++]);
				}
				if (!budget.rows)
					return;
				--budget.rows;
				s.registry->seal();
				s.phase = Impl::Phase::Done;
				continue;
			}
			if (!budget.rows)
				return;
			s.walk->step(budget);
			if (s.walk->missing()) {
				s.phase = Impl::Phase::Missing;
				return;
			}
			if (!s.walk->done())
				return;
			require(s.inputs->prior ? s.inputs->mappings.size() <= s.root.segments
									: s.inputs->mappings.size() == s.root.segments);
			s.position = 0;
			s.phase = Impl::Phase::Register;
		}
	} catch (...) {
		s.failed = true;
		throw;
	}
}
std::optional<StorageIdentity> DependencyDiscovery::missing() const {
	if (!impl_ || impl_->failed || impl_->discarding)
		throw std::logic_error("dependency discovery unavailable");
	if (impl_->phase != Impl::Phase::Missing)
		return {};
	return impl_->walk->missing();
}
void DependencyDiscovery::supply(Handle mapping) {
	if (!impl_ || impl_->failed || impl_->discarding || impl_->phase != Impl::Phase::Missing)
		throw std::logic_error("dependency discovery is not requesting a segment");
	auto &s = *impl_;
	require(mapping && mapping->ownership().segment == *missing() &&
			mapping->ownership().storage == s.storage && s.inputs->mappings.size() < s.root.segments &&
			mapping->ownership().length <= s.limits.bytes - s.physical);
	s.physical += mapping->ownership().length;
	s.pending = std::move(mapping);
	s.hash.reset();
	s.offset = 0;
	s.phase = Impl::Phase::Hash;
}
bool DependencyDiscovery::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->phase == Impl::Phase::Done;
}
d::RetirementHandle<SegmentRegistry> DependencyDiscovery::take() {
	if (!done() || !impl_->registry)
		throw std::logic_error("dependency discovery incomplete");
	return std::move(impl_->registry);
}
bool DependencyDiscovery::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	impl_->discarding = true;
	if (impl_->walk) {
		if (!impl_->walk->discard(budget))
			return false;
		impl_->walk.reset();
	}
	while (!impl_->inputs->mappings.empty() && budget.rows) {
		--budget.rows;
		impl_->inputs->mappings.pop_back();
	}
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
