#include "detent_distribution/subscription_admission.hh"
#include <array>
#include <map>
#include <mutex>

namespace detent::distribution {
namespace d = distribution;
namespace {
std::size_t ordinal(SubscriptionRequestKind kind) {
	switch (kind) {
	case SubscriptionRequestKind::Open:
		return 0;
	case SubscriptionRequestKind::Next:
		return 1;
	case SubscriptionRequestKind::Close:
		return 2;
	}
	throw std::invalid_argument("invalid subscription request kind");
}
} // namespace
struct SubscriptionRequests::Impl {
	using Counts = std::array<std::size_t, 3>;
	using Entry = std::pair<const d::ScopeIdentity, Counts>;
	std::shared_ptr<d::StorageLedger> account;
	SubscriptionRequestLimits limits;
	mutable std::mutex mutex;
	Counts counts{}, capacities{}, per_principal{};
	std::map<d::ScopeIdentity, Counts, std::less<>, d::StorageAllocator<Entry>> principals;
	bool closed = false;
	Impl(std::shared_ptr<d::StorageLedger> ledger, SubscriptionRequestLimits bounds)
		: account(std::move(ledger)), limits(bounds), capacities{bounds.opens, bounds.polls, bounds.closes},
		  per_principal{bounds.per_principal_controls, bounds.per_principal_polls,
						bounds.per_principal_controls},
		  principals(d::StorageAllocator<Entry>(account)) {
		for (std::size_t i = 0; i < capacities.size(); ++i)
			if (!capacities[i] || capacities[i] > 65536 || !per_principal[i] || per_principal[i] > 65536)
				throw std::invalid_argument("invalid subscription request count limit");
		if (!bounds.open_bytes || bounds.open_bytes > 1024 * 1024 || !bounds.control_bytes ||
			bounds.control_bytes > 16384)
			throw std::invalid_argument("invalid subscription body limit");
	}
	std::size_t body_limit(SubscriptionRequestKind kind) const noexcept {
		return kind == SubscriptionRequestKind::Open ? limits.open_bytes : limits.control_bytes;
	}
};
SubscriptionRequests::SubscriptionRequests(std::shared_ptr<d::StorageLedger> account,
										   SubscriptionRequestLimits limits)
	: impl_(std::allocate_shared<Impl>(d::StorageAllocator<Impl>(account), account, limits)) {}
SubscriptionRequests::Lease::Lease(std::shared_ptr<Impl> owner, d::ScopeIdentity principal,
								   SubscriptionRequestKind kind, std::shared_ptr<d::OwnedBuffer> body)
	: owner_(std::move(owner)), principal_(principal), kind_(kind), body_(std::move(body)) {}
SubscriptionRequests::Lease::~Lease() {
	body_.reset();
	if (!active_)
		return;
	std::lock_guard lock(owner_->mutex);
	const auto index = ordinal(kind_);
	--owner_->counts[index];
	auto found = owner_->principals.find(principal_);
	--found->second[index];
	if (found->second == Impl::Counts{})
		owner_->principals.erase(found);
}
std::shared_ptr<SubscriptionRequests::Lease> SubscriptionRequests::admit(d::ScopeIdentity principal,
																		 SubscriptionRequestKind kind) {
	if (principal == d::ScopeIdentity{})
		throw std::invalid_argument("missing subscription principal");
	const auto index = ordinal(kind);
	auto &p = *impl_;
	std::lock_guard lock(p.mutex);
	auto found = p.principals.find(principal);
	if (p.closed || p.counts[index] >= p.capacities[index] ||
		(found != p.principals.end() && found->second[index] >= p.per_principal[index]))
		return {};
	auto body = d::make_owned_buffer(p.account, p.body_limit(kind));
	auto lease = std::allocate_shared<Lease>(d::StorageAllocator<Lease>(p.account), impl_, principal, kind,
											 std::move(body));
	if (found == p.principals.end())
		found = p.principals.emplace(principal, Impl::Counts{}).first;
	++p.counts[index];
	++found->second[index];
	lease->active_ = true;
	return lease;
}
void SubscriptionRequests::close() noexcept {
	std::lock_guard lock(impl_->mutex);
	impl_->closed = true;
}
bool SubscriptionRequests::drained() const noexcept {
	std::lock_guard lock(impl_->mutex);
	return impl_->counts == Impl::Counts{};
}
std::size_t SubscriptionRequests::body_limit(SubscriptionRequestKind kind) const noexcept {
	return impl_->body_limit(kind);
}
} // namespace detent::distribution
