#include "subscription.hh"
#include <algorithm>
#include <limits>
#include <map>

namespace detent::authority {
namespace d = distribution;
struct Subscriptions::Impl {
	struct Offer {
		std::uint64_t window;
		d::Cursor target;
		std::optional<ChangeHistory::Handoff> marker;
	};
	struct Session {
		SubscriptionMetadata metadata;
		d::Identity id{};
		std::uint64_t incarnation = 0, serial = 0, accepted = 0;
		std::optional<d::Cursor> base;
		std::optional<ChangeHistory::Handoff> marker;
		std::optional<Offer> offered;
		Clock::time_point idle, maximum;
		bool snapshot = false, request = false, transfer = false, closing = false;
	};
	struct Slot {
		std::optional<Session> session;
		std::uint64_t incarnation = 0;
		std::size_t retirement_remaining = 0;
		std::shared_ptr<const AuthorizedScope> retiring_scope;
		bool retiring = false;
	};
	using Slots = std::vector<Slot, d::StorageAllocator<Slot>>;
	using Free = std::vector<std::size_t, d::StorageAllocator<std::size_t>>;
	using IndexValue = std::pair<const d::Identity, std::size_t>;
	using Index = std::map<d::Identity, std::size_t, std::less<>, d::StorageAllocator<IndexValue>>;
	using PrincipalValue = std::pair<const d::ScopeIdentity, std::size_t>;
	using Principals =
		std::map<d::ScopeIdentity, std::size_t, std::less<>, d::StorageAllocator<PrincipalValue>>;
	std::shared_ptr<d::StorageLedger> account;
	SubscriptionLimits limits;
	Slots slots;
	Free free;
	Index index;
	Principals principals;
	std::size_t cursor = 0;
	bool closed = false;
	Impl(std::shared_ptr<d::StorageLedger> ledger, SubscriptionLimits bounds)
		: account(std::move(ledger)), limits(bounds), slots(d::StorageAllocator<Slot>(account)),
		  free(d::StorageAllocator<std::size_t>(account)), index(d::StorageAllocator<IndexValue>(account)),
		  principals(d::StorageAllocator<PrincipalValue>(account)) {
		if (!limits.sessions || limits.sessions > 65536 || !limits.per_principal ||
			limits.per_principal > limits.sessions || limits.idle_lifetime <= Clock::duration::zero() ||
			limits.maximum_lifetime < limits.idle_lifetime ||
			limits.maximum_lifetime > std::chrono::hours(24))
			throw std::invalid_argument("invalid subscription limits");
		slots.resize(limits.sessions);
		free.reserve(limits.sessions);
		for (std::size_t i = limits.sessions; i; --i)
			free.push_back(i - 1);
	}
	Session *find(d::Identity id) noexcept {
		auto found = index.find(id);
		return found == index.end() ? nullptr : &*slots[found->second].session;
	}
	const Session *find(d::Identity id) const noexcept {
		auto found = index.find(id);
		return found == index.end() ? nullptr : &*slots[found->second].session;
	}
	Session *find(SubscriptionRequestToken token) noexcept {
		auto *session = find(token.session);
		return session && session->incarnation == token.incarnation && session->serial == token.request &&
					   session->request
				   ? session
				   : nullptr;
	}
	bool expire(Session &session, Clock::time_point now) noexcept {
		if (closed || now >= session.idle || now >= session.maximum)
			session.closing = true;
		return session.closing;
	}
};
Subscriptions::Subscriptions(std::shared_ptr<d::StorageLedger> account, SubscriptionLimits limits)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, limits)) {}
Subscriptions::~Subscriptions() = default;
SubscriptionResult Subscriptions::open(d::Identity id, SubscriptionPrincipal principal,
									   std::shared_ptr<const AuthorizedScope> scope, d::Cursor current,
									   std::optional<d::Cursor> base, bool force_snapshot, bool compression,
									   ChangeHistory &history, Clock::time_point now) {
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
	if (p.index.contains(id))
		return SubscriptionResult::Busy;
	auto found = p.principals.find(principal.identity);
	if (p.free.empty() || (found != p.principals.end() && found->second == p.limits.per_principal))
		return SubscriptionResult::Full;
	std::optional<ChangeHistory::Handoff> marker;
	if (base && !force_snapshot) {
		Context context{current.cluster, current.group, current.restore_epoch};
		marker = history.handoff(context, base->revision);
		if (marker && history.continuity(*marker, current.revision) != ChangeHistory::Continuity::Ready)
			marker.reset();
	}
	const auto slot = p.free.back();
	auto &entry = p.slots[slot];
	if (entry.incarnation == UINT64_MAX)
		return SubscriptionResult::Full;
	bool principal_added = false;
	if (found == p.principals.end()) {
		auto inserted = p.principals.emplace(principal.identity, 0);
		found = inserted.first;
		principal_added = inserted.second;
	}
	try {
		p.index.emplace(id, slot);
	} catch (...) {
		if (principal_added)
			p.principals.erase(found);
		throw;
	}
	Impl::Session session;
	session.metadata = {principal, std::move(scope), current, compression};
	session.metadata.identity.revision = 0;
	session.id = id;
	session.incarnation = ++entry.incarnation;
	session.base = base;
	session.marker = std::move(marker);
	session.snapshot = !session.marker;
	session.idle = now + p.limits.idle_lifetime;
	session.maximum = now + p.limits.maximum_lifetime;
	entry.session.emplace(std::move(session));
	++found->second;
	p.free.pop_back();
	return SubscriptionResult::Accepted;
}
const SubscriptionMetadata *Subscriptions::metadata(d::Identity id, Clock::time_point now) noexcept {
	auto *session = impl_->find(id);
	return !session || impl_->expire(*session, now) ? nullptr : &session->metadata;
}
SubscriptionAdmission Subscriptions::next(d::Identity id, SubscriptionPrincipal principal,
										  std::uint64_t after, std::optional<d::Cursor> published,
										  Clock::time_point now) {
	auto &p = *impl_;
	auto *s = p.find(id);
	if (!s || p.expire(*s, now))
		return {SubscriptionResult::Expired, {}};
	if (principal.identity != s->metadata.principal.identity)
		return {SubscriptionResult::Forbidden, {}};
	if (principal.policy_epoch != s->metadata.principal.policy_epoch)
		return {SubscriptionResult::IdentityChanged, {}};
	if (s->request)
		return {SubscriptionResult::Busy, {}};
	if (s->transfer && (!s->offered || after != s->offered->window || !published))
		return {SubscriptionResult::Busy, {}};
	bool valid = s->offered ? after == s->offered->window && published && *published == s->offered->target
							: after == s->accepted &&
								  (!published || (s->accepted && s->base && *published == *s->base));
	if (!valid)
		return {SubscriptionResult::OutOfSync, {}};
	if (s->serial == UINT64_MAX) {
		s->closing = true;
		return {SubscriptionResult::Expired, {}};
	}
	s->request = true;
	++s->serial;
	s->idle = std::min(now + p.limits.idle_lifetime, s->maximum);
	return {SubscriptionResult::Accepted, SubscriptionRequestToken{id, s->incarnation, s->serial}};
}
SubscriptionResult Subscriptions::poll(SubscriptionRequestToken token, d::Cursor current,
									   ChangeHistory &history, Clock::time_point now) noexcept {
	auto &p = *impl_;
	auto *s = p.find(token);
	if (!s || p.expire(*s, now))
		return SubscriptionResult::Expired;
	if (!current.same_identity(s->metadata.identity)) {
		s->closing = true;
		return SubscriptionResult::IdentityChanged;
	}
	if (s->transfer)
		return SubscriptionResult::AwaitSettlement;
	if (s->offered) {
		s->accepted = s->offered->window;
		s->base = s->offered->target;
		s->marker = std::move(s->offered->marker);
		s->offered.reset();
		s->snapshot = false;
	}
	if (s->base && current.revision < s->base->revision) {
		s->closing = true;
		return SubscriptionResult::OutOfSync;
	}
	if (s->snapshot)
		return SubscriptionResult::SnapshotReady;
	if (!s->base) {
		s->closing = true;
		return SubscriptionResult::OutOfSync;
	}
	if (!s->marker || history.continuity(*s->marker, current.revision) != ChangeHistory::Continuity::Ready) {
		s->closing = true;
		return SubscriptionResult::SnapshotRequired;
	}
	return current.revision == s->base->revision ? SubscriptionResult::Parked
												 : SubscriptionResult::ResumeReady;
}
std::optional<d::Cursor> Subscriptions::base(SubscriptionRequestToken token) const noexcept {
	auto *s = impl_->find(token.session);
	return s && s->incarnation == token.incarnation && s->serial == token.request && s->request
			   ? s->base
			   : std::nullopt;
}
std::optional<ChangeHistory::Handoff> Subscriptions::handoff(SubscriptionRequestToken token) const noexcept {
	auto *s = impl_->find(token.session);
	return s && s->incarnation == token.incarnation && s->serial == token.request && s->request
			   ? s->marker
			   : std::nullopt;
}
SubscriptionOfferToken Subscriptions::offer(SubscriptionRequestToken token, d::Cursor target,
											std::optional<ChangeHistory::Handoff> marker,
											Clock::time_point now) {
	auto &p = *impl_;
	auto *s = p.find(token);
	if (!s || p.expire(*s, now) || s->transfer || s->offered || s->accepted == UINT64_MAX ||
		!target.same_identity(s->metadata.identity) || (marker && marker->revision() != target.revision) ||
		(s->base && target.revision < s->base->revision) ||
		(!s->snapshot && (!s->base || target.revision <= s->base->revision)))
		throw std::logic_error("subscription offer state mismatch");
	s->offered.emplace(Impl::Offer{s->accepted + 1, target, std::move(marker)});
	s->transfer = true;
	s->request = false;
	return {s->id, s->incarnation, s->offered->window};
}
void Subscriptions::settle(SubscriptionOfferToken token, bool written) noexcept {
	auto *s = impl_->find(token.session);
	if (!s || s->incarnation != token.incarnation || !s->transfer || !s->offered ||
		s->offered->window != token.window)
		return;
	s->transfer = false;
	if (!written)
		s->closing = true;
}
void Subscriptions::release(SubscriptionRequestToken token) noexcept {
	if (auto *s = impl_->find(token)) {
		s->request = false;
	}
}
SubscriptionResult Subscriptions::close(d::Identity id, SubscriptionPrincipal principal) noexcept {
	auto *s = impl_->find(id);
	if (!s)
		return SubscriptionResult::Accepted;
	if (s->metadata.principal.identity != principal.identity)
		return SubscriptionResult::Forbidden;
	if (s->metadata.principal.policy_epoch != principal.policy_epoch)
		return SubscriptionResult::IdentityChanged;
	s->closing = true;
	return SubscriptionResult::Accepted;
}
void Subscriptions::close_all() noexcept { impl_->closed = true; }
void Subscriptions::collect(std::size_t count, Clock::time_point now) noexcept {
	d::WorkBudget budget{8192, count};
	collect(budget, now);
}
void Subscriptions::collect(d::WorkBudget &budget, Clock::time_point now) noexcept {
	auto &p = *impl_;
	const auto count = std::min(budget.tokens, p.slots.size());
	for (std::size_t i = 0; i < count; ++i) {
		--budget.tokens;
		auto &slot = p.slots[p.cursor];
		const auto ordinal = p.cursor;
		p.cursor = (p.cursor + 1) % p.slots.size();
		if (!slot.session || !p.expire(*slot.session, now) || slot.session->request || slot.session->transfer)
			continue;
		if (!slot.retiring) {
			slot.retiring = true;
			auto scope = std::move(slot.session->metadata.scope);
			if (scope.use_count() == 1) {
				slot.retirement_remaining = scope->storage_capacity();
				slot.retiring_scope = std::move(scope);
			}
		}
		const auto bytes = std::min(slot.retirement_remaining, budget.bytes);
		slot.retirement_remaining -= bytes;
		budget.bytes -= bytes;
		if (slot.retirement_remaining) {
			p.cursor = ordinal;
			return;
		}
		auto found = p.principals.find(slot.session->metadata.principal.identity);
		if (!--found->second)
			p.principals.erase(found);
		p.index.erase(slot.session->id);
		slot.retiring_scope.reset();
		slot.session.reset();
		slot.retiring = false;
		if (slot.incarnation != UINT64_MAX)
			p.free.push_back(ordinal);
	}
}
std::size_t Subscriptions::size() const noexcept { return impl_->index.size(); }
bool Subscriptions::drained() const noexcept { return impl_->index.empty(); }
} // namespace detent::authority
