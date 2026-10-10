#include "detent_relay/sessions.hh"

namespace detent::relay {
namespace d = distribution;
struct Sessions::Impl {
	using Table = d::SessionTable<d::CacheMarker, std::size_t>;
	std::vector<SessionBinding, d::StorageAllocator<SessionBinding>> bindings;
	Table table;
	Impl(std::shared_ptr<d::StorageLedger> account, std::span<const SessionBinding> configured,
		 d::SubscriptionLimits limits)
		: bindings(d::StorageAllocator<SessionBinding>(account)), table(account, limits) {
		if (!account || configured.empty() || configured.size() > 256)
			throw std::invalid_argument("relay session binding limit");
		for (const auto &binding : configured) {
			if (!binding.bundle || binding.principal.identity == d::ScopeIdentity{} ||
				!binding.principal.policy_epoch || binding.identity.cluster == d::Identity{} ||
				binding.identity.group == d::Identity{} || binding.identity.restore_epoch == d::Identity{} ||
				binding.identity.scope == d::ScopeIdentity{})
				throw std::invalid_argument("invalid relay session binding");
			for (const auto &prior : bindings)
				if (prior.identity.same_identity(binding.identity))
					throw std::invalid_argument("duplicate relay session binding");
			bindings.push_back(binding);
		}
	}
	d::SubscriptionResult authorize(d::Identity id, const SessionAuthorization &authorization,
									Clock::time_point now, bool allow_fenced = false) noexcept {
		auto *metadata = table.metadata(id, now);
		if (!metadata)
			return d::SubscriptionResult::Expired;
		if (!authorization.allows(metadata->scope))
			return d::SubscriptionResult::Forbidden;
		const auto &binding = bindings[metadata->scope];
		if (!allow_fenced && binding.bundle->status().fenced)
			return d::SubscriptionResult::Forbidden;
		return table.authorize(id, metadata->incarnation, authorization.principal, binding.identity.scope,
							   authorization.grants, now);
	}
};
Sessions::Sessions(std::shared_ptr<d::StorageLedger> account, std::span<const SessionBinding> configured,
				   d::SubscriptionLimits limits)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, configured, limits)) {}
Sessions::~Sessions() = default;
d::SubscriptionResult Sessions::open(d::Identity id, const SessionAuthorization &authorization,
									 std::size_t index, std::optional<d::Cursor> base, bool force_snapshot,
									 bool compression, Clock::time_point now) {
	auto &p = *impl_;
	if (index >= p.bindings.size() || !authorization.allows(index))
		return d::SubscriptionResult::Forbidden;
	const auto &binding = p.bindings[index];
	if (authorization.principal.identity != binding.principal.identity)
		return d::SubscriptionResult::Forbidden;
	if (authorization.principal.policy_epoch != binding.principal.policy_epoch)
		return d::SubscriptionResult::IdentityChanged;
	const auto status = binding.bundle->status();
	if (status.fenced)
		return d::SubscriptionResult::Forbidden;
	if (!status.ready || !status.selected)
		return d::SubscriptionResult::Busy;
	if (!status.selected->same_identity(binding.identity))
		return d::SubscriptionResult::IdentityChanged;
	auto candidate = binding.bundle->offer(base, force_snapshot);
	if (candidate.decision == d::CacheDecision::SourceBehind)
		return d::SubscriptionResult::SourceBehind;
	if (candidate.decision == d::CacheDecision::IdentityChanged)
		return d::SubscriptionResult::IdentityChanged;
	std::optional<d::CacheMarker> marker;
	if (!force_snapshot && (candidate.decision == d::CacheDecision::Idle ||
							(candidate.decision == d::CacheDecision::Update &&
							 candidate.update->kind() != d::UpdateKind::Snapshot)))
		marker = candidate.marker;
	return p.table.open(id, authorization.principal, index, *status.selected, base, force_snapshot,
						compression, marker, now, authorization.grants);
}
const Sessions::Metadata *Sessions::metadata(d::Identity id, Clock::time_point now) noexcept {
	return impl_->table.metadata(id, now);
}
d::SubscriptionAdmission Sessions::next(d::Identity id, const SessionAuthorization &authorization,
										std::uint64_t after, std::optional<d::Cursor> published,
										Clock::time_point now) {
	const auto authorized = impl_->authorize(id, authorization, now);
	if (authorized != d::SubscriptionResult::Accepted)
		return {authorized, {}};
	return impl_->table.next(id, authorization.principal, after, published, now);
}
PreparedOffer Sessions::prepare(d::SubscriptionRequestToken token, Clock::time_point now) {
	auto &p = *impl_;
	auto *metadata = p.table.metadata(token.session, now);
	if (!metadata || metadata->incarnation != token.incarnation)
		return {d::SubscriptionResult::Expired, {}, {}};
	auto &bundle = *p.bindings[metadata->scope].bundle;
	const auto status = bundle.status();
	if (status.fenced) {
		p.table.close(token.session, metadata->principal, token.incarnation);
		return {d::SubscriptionResult::Forbidden, {}, {}};
	}
	if (!status.ready || !status.selected)
		return {d::SubscriptionResult::Busy, {}, {}};
	const auto result = p.table.poll(
		token, *status.selected,
		[&bundle](const auto &marker, const auto &base, const auto &) noexcept {
			return bundle.covers(marker, base);
		},
		now);
	if (result != d::SubscriptionResult::SnapshotReady && result != d::SubscriptionResult::ResumeReady)
		return {result, {}, {}};
	auto candidate = bundle.offer(p.table.base(token), result == d::SubscriptionResult::SnapshotReady);
	switch (candidate.decision) {
	case d::CacheDecision::Update: {
		auto offered = p.table.offer(token, candidate.update->target(), candidate.marker, now);
		return {result, offered, std::move(candidate.update)};
	}
	case d::CacheDecision::SourceBehind:
		return {d::SubscriptionResult::SourceBehind, {}, {}};
	case d::CacheDecision::SnapshotRequired:
		return {d::SubscriptionResult::SnapshotRequired, {}, {}};
	case d::CacheDecision::IdentityChanged:
		return {d::SubscriptionResult::IdentityChanged, {}, {}};
	case d::CacheDecision::Fenced:
		return {d::SubscriptionResult::Forbidden, {}, {}};
	case d::CacheDecision::Idle:
		return {d::SubscriptionResult::Parked, {}, {}};
	case d::CacheDecision::Busy:
		return {d::SubscriptionResult::Busy, {}, {}};
	}
	throw std::logic_error("invalid relay cache decision");
}
void Sessions::settle(d::SubscriptionOfferToken token, bool written) noexcept {
	impl_->table.settle(token, written);
}
void Sessions::release(d::SubscriptionRequestToken token) noexcept { impl_->table.release(token); }
d::SubscriptionResult Sessions::close(d::Identity id, const SessionAuthorization &authorization,
									  Clock::time_point now) noexcept {
	if (!impl_->table.metadata(id, now))
		return d::SubscriptionResult::Accepted;
	const auto authorized = impl_->authorize(id, authorization, now, true);
	return authorized == d::SubscriptionResult::Accepted ? impl_->table.close(id, authorization.principal)
														 : authorized;
}
void Sessions::close_all() noexcept { impl_->table.close_all(); }
void Sessions::collect(d::WorkBudget &budget, Clock::time_point now) noexcept {
	impl_->table.collect(budget, now);
}
std::size_t Sessions::size() const noexcept { return impl_->table.size(); }
bool Sessions::drained() const noexcept { return impl_->table.drained(); }
} // namespace detent::relay
