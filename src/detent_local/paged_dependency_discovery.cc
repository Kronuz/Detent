#include "paged_dependency_discovery.hh"
#include "paged_navigation.hh"
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
	Inputs(std::shared_ptr<d::StorageLedger> account, std::size_t capacity)
		: mappings(d::StorageAllocator<Handle>(account)) {
		mappings.reserve(capacity);
	}
};
} // namespace
struct DependencyDiscovery::Impl {
	enum class Phase { Walk, Missing, Hash, Insert, Register, Done };
	struct Frame {
		p::Reference reference;
		std::span<const char> bytes;
		p::PageHeader header;
		std::size_t context = 0, children = 0;
		bool checked = false;
	};
	std::shared_ptr<d::StorageLedger> account;
	std::shared_ptr<Inputs> inputs;
	d::RetirementHandle<SegmentRegistry> registry;
	p::Root root;
	StorageIdentity storage;
	p::Limits limits;
	std::array<Frame, 203> frames{};
	std::optional<PageValidator> page;
	Handle pending;
	SHA256 hash;
	std::size_t depth = 0, offset = 0, position = 0, moving = 0, charge = 0;
	std::uint64_t physical = 0, pages = 0, extents = 0;
	Phase phase = Phase::Walk;
	bool failed = false, discarding = false;
	Impl(std::shared_ptr<d::StorageLedger> ledger, d::RetirementDomain &domain, p::Root expected,
		 StorageIdentity identity, p::Limits bounds)
		: account(std::move(ledger)), root(expected), storage(identity), limits(bounds) {
		p::encode_root(root, limits);
		require(root.segments <= p::Limits{}.segments);
		inputs = std::allocate_shared<Inputs>(d::StorageAllocator<Inputs>(account), account, root.segments);
		auto registry_limits = limits;
		registry_limits.segments = root.segments;
		registry = domain.make<SegmentRegistry>(account, storage, registry_limits);
		if (root.pages)
			push(root.tree, 0);
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
	void push(p::Reference reference, std::size_t context) {
		p::encode_reference(reference);
		require(depth < frames.size() && pages < root.pages && reference.length <= root.bytes - extents);
		frames[depth++] = {reference, {}, {}, context};
		++pages;
		extents += reference.length;
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
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, domain, root, identity,
										limits)) {}
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
			if (!s.depth) {
				require(s.pages == s.root.pages && s.extents == s.root.bytes &&
						s.inputs->mappings.size() == s.root.segments);
				s.position = 0;
				s.phase = Impl::Phase::Register;
				continue;
			}
			auto &frame = s.frames[s.depth - 1];
			if (!frame.checked) {
				if (!s.page) {
					auto found = s.find(frame.reference.segment);
					if (found == s.inputs->mappings.end() ||
						(*found)->ownership().segment != frame.reference.segment) {
						s.phase = Impl::Phase::Missing;
						return;
					}
					if (!s.admit(p::reference_bytes, budget))
						return;
					SegmentInput input{(*found)->ownership(), (*found)->bytes()};
					frame.bytes = checked_page_bytes(input, frame.reference);
					s.page.emplace(s.account, s.inputs, frame.bytes, frame.reference, s.limits);
				}
				s.page->step(budget);
				if (!s.page->done())
					return;
				frame.header = s.page->header();
				if (!s.page->discard(budget))
					return;
				s.page.reset();
				require(frame.header.prefix <= 202 - frame.context);
				frame.context += frame.header.prefix;
				frame.children = frame.header.children;
				frame.checked = true;
			}
			if (frame.children) {
				if (!s.admit(p::child_bytes + 1, budget))
					return;
				require(frame.context < 202);
				auto child = p::decode_child(frame.bytes.subspan(p::page_header_bytes + frame.header.prefix +
																	 (--frame.children) * p::child_bytes,
																 p::child_bytes));
				s.push(child.reference, frame.context + 1);
			} else {
				if (!budget.rows)
					return;
				--budget.rows;
				--s.depth;
			}
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
	return impl_->frames[impl_->depth - 1].reference.segment;
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
	if (impl_->page) {
		if (!impl_->page->discard(budget))
			return false;
		impl_->page.reset();
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
