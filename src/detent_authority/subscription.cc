#include "subscription.hh"

namespace detent::authority {
namespace d = distribution;
struct Subscriptions::Impl {
	using Binding = std::shared_ptr<const AuthorizedScope>;
	using Table = d::SessionTable<ChangeHistory::Handoff, Binding>;
	std::shared_ptr<d::StorageLedger> account;
	Table table;
	bool closed = false;
	static std::size_t retirement_capacity(const Binding &scope) noexcept {
		return scope && scope.use_count() == 1 ? scope->storage_capacity() : 0;
	}
	Impl(std::shared_ptr<d::StorageLedger> ledger, SubscriptionLimits limits)
		: account(std::move(ledger)), table(account, limits, retirement_capacity) {}
};
Subscriptions::Subscriptions(std::shared_ptr<d::StorageLedger> account, SubscriptionLimits limits)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, limits)) {}
Subscriptions::~Subscriptions() = default;
SubscriptionResult Subscriptions::open(d::Identity id, SubscriptionPrincipal principal,
									   std::shared_ptr<const AuthorizedScope> scope, d::Cursor current,
									   std::optional<d::Cursor> base, bool force_snapshot, bool compression,
									   ChangeHistory &history, Clock::time_point now,
									   std::optional<d::ScopeIdentity> grants) {
	auto &p = *impl_;
	if (p.closed)
		return SubscriptionResult::Expired;
	if (id == d::Identity{} || principal.identity == d::ScopeIdentity{} || !principal.policy_epoch ||
		!scope || scope->storage_account() != p.account || history.storage_account() != p.account ||
		current.scope != scope->identity() || current.cluster == d::Identity{} ||
		current.group == d::Identity{} || current.restore_epoch == d::Identity{})
		throw std::invalid_argument("invalid subscription binding");
	if (base && !base->same_identity(current))
		return SubscriptionResult::IdentityChanged;
	if (base && base->revision > current.revision)
		return SubscriptionResult::OutOfSync;
	std::optional<ChangeHistory::Handoff> marker;
	if (base && !force_snapshot) {
		Context context{current.cluster, current.group, current.restore_epoch};
		marker = history.handoff(context, base->revision);
		if (marker && history.continuity(*marker, current.revision) != ChangeHistory::Continuity::Ready)
			marker.reset();
	}
	return p.table.open(id, principal, std::move(scope), current, base, force_snapshot, compression,
						std::move(marker), now, grants);
}
const SubscriptionMetadata *Subscriptions::metadata(d::Identity id, Clock::time_point now) noexcept {
	return impl_->table.metadata(id, now);
}
SubscriptionAdmission Subscriptions::next(d::Identity id, SubscriptionPrincipal principal,
										  std::uint64_t after, std::optional<d::Cursor> published,
										  Clock::time_point now, std::uint64_t incarnation) {
	return impl_->table.next(id, principal, after, published, now, incarnation);
}
SubscriptionResult Subscriptions::authorize(d::Identity id, std::uint64_t incarnation,
											SubscriptionPrincipal principal, d::ScopeIdentity scope,
											d::ScopeIdentity grants, Clock::time_point now) noexcept {
	return impl_->table.authorize(id, incarnation, principal, scope, grants, now);
}
SubscriptionResult Subscriptions::poll(SubscriptionRequestToken token, d::Cursor current,
									   ChangeHistory &history, Clock::time_point now) noexcept {
	const auto result = impl_->table.poll(
		token, current,
		[&history](const auto &marker, const auto &, const auto &target) noexcept {
			return history.continuity(marker, target.revision) == ChangeHistory::Continuity::Ready;
		},
		now);
	if (result != SubscriptionResult::SourceBehind)
		return result;
	if (auto *metadata = impl_->table.metadata(token.session, now))
		impl_->table.close(token.session, metadata->principal, token.incarnation);
	return SubscriptionResult::OutOfSync;
}
std::optional<d::Cursor> Subscriptions::base(SubscriptionRequestToken token) const noexcept {
	return impl_->table.base(token);
}
std::optional<ChangeHistory::Handoff> Subscriptions::handoff(SubscriptionRequestToken token) const noexcept {
	return impl_->table.handoff(token);
}
SubscriptionOfferToken Subscriptions::offer(SubscriptionRequestToken token, d::Cursor target,
											std::optional<ChangeHistory::Handoff> marker,
											Clock::time_point now) {
	if (marker && marker->revision() != target.revision)
		throw std::logic_error("subscription offer state mismatch");
	return impl_->table.offer(token, target, std::move(marker), now);
}
void Subscriptions::settle(SubscriptionOfferToken token, bool written) noexcept {
	impl_->table.settle(token, written, Clock::now());
}
void Subscriptions::release(SubscriptionRequestToken token) noexcept { impl_->table.release(token, Clock::now()); }
SubscriptionResult Subscriptions::close(d::Identity id, SubscriptionPrincipal principal,
										std::uint64_t incarnation) noexcept {
	return impl_->table.close(id, principal, incarnation);
}
void Subscriptions::close_all() noexcept {
	impl_->closed = true;
	impl_->table.close_all();
}
void Subscriptions::collect(std::size_t count, Clock::time_point now) noexcept {
	impl_->table.collect(count, now);
}
void Subscriptions::collect(d::WorkBudget &budget, Clock::time_point now) noexcept {
	impl_->table.collect(budget, now);
}
std::size_t Subscriptions::size() const noexcept { return impl_->table.size(); }
bool Subscriptions::drained() const noexcept { return impl_->table.drained(); }
} // namespace detent::authority
