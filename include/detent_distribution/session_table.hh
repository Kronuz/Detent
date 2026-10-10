#pragma once
#include "detent_distribution/protocol.hh"
#include "detent_distribution/publication_window.hh"
#include <algorithm>
#include <chrono>
#include <map>

namespace detent::distribution {
struct SubscriptionPrincipal {
	distribution::ScopeIdentity identity{};
	std::uint64_t policy_epoch = 0;
	bool operator==(const SubscriptionPrincipal &) const = default;
};
struct SubscriptionLimits {
	std::size_t sessions = 1024, per_principal = 256;
	std::chrono::milliseconds idle_lifetime{60000}, maximum_lifetime{600000};
};
enum class SubscriptionResult {
	Accepted,
	Parked,
	AwaitSettlement,
	SnapshotReady,
	ResumeReady,
	Busy,
	Expired,
	Forbidden,
	IdentityChanged,
	OutOfSync,
	SnapshotRequired,
	Full,
	SourceBehind
};
struct SubscriptionRequestToken {
	distribution::Identity session{};
	std::uint64_t incarnation = 0, request = 0;
	bool operator==(const SubscriptionRequestToken &) const = default;
};
struct SubscriptionOfferToken {
	distribution::Identity session{};
	std::uint64_t incarnation = 0, window = 0;
};
struct SubscriptionAdmission {
	SubscriptionResult result;
	std::optional<SubscriptionRequestToken> token;
};
template <class Binding> struct SessionMetadata {
	SubscriptionPrincipal principal;
	Binding scope;
	distribution::Cursor identity;
	bool compression = false;
	std::uint64_t incarnation = 0;
	std::optional<distribution::ScopeIdentity> creator_grants, last_authorized_grants;
};
// Owner-local bounded sessions. Adapters authenticate bindings and continuity;
// accepted original writes determine when an offer can settle.
template <class Marker, class Binding> class SessionTable {
    static_assert(std::is_nothrow_copy_constructible_v<Marker> &&
                  std::is_nothrow_move_constructible_v<Marker> &&
                  std::is_nothrow_move_assignable_v<Marker>);
    static_assert(std::is_nothrow_default_constructible_v<Binding> &&
                  std::is_nothrow_move_constructible_v<Binding> &&
                  std::is_nothrow_move_assignable_v<Binding>);
  public:
	using Clock = std::chrono::steady_clock;
	using SubscriptionMetadata = SessionMetadata<Binding>;

  private:
	struct Impl {
		struct Session {
			SubscriptionMetadata metadata;
			Identity id{};
			std::uint64_t incarnation = 0, serial = 0;
			PublicationWindow<Marker> window;
			Clock::time_point idle, maximum;
			bool request = false, closing = false;
		};
		struct Slot {
			std::optional<Session> session;
			std::uint64_t incarnation = 0;
			std::size_t retirement_remaining = 0;
			Binding retiring_scope{};
			bool retiring = false;
		};
		using Slots = std::vector<Slot, StorageAllocator<Slot>>;
		using Free = std::vector<std::size_t, StorageAllocator<std::size_t>>;
		using IndexValue = std::pair<const Identity, std::size_t>;
		using Index = std::map<Identity, std::size_t, std::less<>, StorageAllocator<IndexValue>>;
		using PrincipalValue = std::pair<const ScopeIdentity, std::size_t>;
		using Principals =
			std::map<ScopeIdentity, std::size_t, std::less<>, StorageAllocator<PrincipalValue>>;
		std::shared_ptr<StorageLedger> account;
		SubscriptionLimits limits;
		Slots slots;
		Free free;
		Index index;
		Principals principals;
		std::size_t cursor = 0;
		bool closed = false;
		Impl(std::shared_ptr<StorageLedger> ledger, SubscriptionLimits bounds)
			: account(std::move(ledger)), limits(bounds), slots(StorageAllocator<Slot>(account)),
			  free(StorageAllocator<std::size_t>(account)), index(StorageAllocator<IndexValue>(account)),
			  principals(StorageAllocator<PrincipalValue>(account)) {
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
		Session *find(Identity id) noexcept {
			auto found = index.find(id);
			return found == index.end() ? nullptr : &*slots[found->second].session;
		}
		const Session *find(Identity id) const noexcept {
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
	std::size_t (*retirement_capacity_)(const Binding &) noexcept;
	StorageOwner<Impl> impl_;

  public:
	SessionTable(std::shared_ptr<StorageLedger> account, SubscriptionLimits limits,
				 std::size_t (*retirement_capacity)(const Binding &) noexcept = nullptr)
		: retirement_capacity_(retirement_capacity),
		  impl_(make_storage_owner<Impl>(account, std::malloc, std::free, account, limits)) {}
	~SessionTable() = default;
	SubscriptionResult open(Identity id, SubscriptionPrincipal principal, Binding scope, Cursor current,
							std::optional<Cursor> base, bool force_snapshot, bool compression,
							std::optional<Marker> marker, Clock::time_point now,
							std::optional<ScopeIdentity> grants = {}) {
		auto &p = *impl_;
		if (p.closed)
			return SubscriptionResult::Expired;
		if (id == Identity{} || principal.identity == ScopeIdentity{} || !principal.policy_epoch ||
			current.scope == ScopeIdentity{} || current.cluster == Identity{} ||
			current.group == Identity{} || current.restore_epoch == Identity{})
			throw std::invalid_argument("invalid subscription binding");
		if (base && !base->same_identity(current))
			return SubscriptionResult::IdentityChanged;
		if (base && base->revision > current.revision)
			return SubscriptionResult::SourceBehind;
		if (p.index.contains(id))
			return SubscriptionResult::Busy;
		auto found = p.principals.find(principal.identity);
		if (p.free.empty() || (found != p.principals.end() && found->second == p.limits.per_principal))
			return SubscriptionResult::Full;
		if (force_snapshot || !base)
			marker.reset();
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
		typename Impl::Session session;
		session.metadata = {principal, std::move(scope), current, compression};
		session.metadata.identity.revision = 0;
		session.id = id;
		session.incarnation = ++entry.incarnation;
		session.metadata.incarnation = session.incarnation;
		session.metadata.creator_grants = std::move(grants);
		session.window = PublicationWindow<Marker>{base, marker, !marker};
		session.idle = now + p.limits.idle_lifetime;
		session.maximum = now + p.limits.maximum_lifetime;
		entry.session.emplace(std::move(session));
		++found->second;
		p.free.pop_back();
		return SubscriptionResult::Accepted;
	}
	const SubscriptionMetadata *metadata(Identity id, Clock::time_point now) noexcept {
		auto *session = impl_->find(id);
		return !session || impl_->expire(*session, now) ? nullptr : &session->metadata;
	}
	SubscriptionAdmission next(Identity id, SubscriptionPrincipal principal, std::uint64_t after,
							   std::optional<Cursor> published, Clock::time_point now,
							   std::uint64_t incarnation = 0) {
		auto &p = *impl_;
		auto *s = p.find(id);
		if (!s || (incarnation && s->incarnation != incarnation) || p.expire(*s, now))
			return {SubscriptionResult::Expired, {}};
		if (principal.identity != s->metadata.principal.identity)
			return {SubscriptionResult::Forbidden, {}};
		if (principal.policy_epoch != s->metadata.principal.policy_epoch)
			return {SubscriptionResult::IdentityChanged, {}};
		if (s->request)
			return {SubscriptionResult::Busy, {}};
		const auto feedback = s->window.feedback(after, published);
		if (feedback == PublicationFeedbackResult::Busy)
			return {SubscriptionResult::Busy, {}};
		if (feedback == PublicationFeedbackResult::OutOfSync)
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
	SubscriptionResult authorize(Identity id, std::uint64_t incarnation, SubscriptionPrincipal principal,
								 ScopeIdentity scope, ScopeIdentity grants, Clock::time_point now) noexcept {
		auto *s = impl_->find(id);
		if (!s || s->incarnation != incarnation || impl_->expire(*s, now))
			return SubscriptionResult::Expired;
		if (s->metadata.principal.identity != principal.identity)
			return SubscriptionResult::Forbidden;
		if (s->metadata.principal.policy_epoch != principal.policy_epoch ||
			s->metadata.identity.scope != scope)
			return SubscriptionResult::IdentityChanged;
		s->metadata.last_authorized_grants = grants;
		return SubscriptionResult::Accepted;
	}
	template <class Continuity>
	SubscriptionResult poll(SubscriptionRequestToken token, Cursor current, Continuity &&continuity,
							Clock::time_point now) noexcept {
		static_assert(
			std::is_nothrow_invocable_r_v<bool, Continuity, const Marker &, const Cursor &, const Cursor &>);
		auto &p = *impl_;
		auto *s = p.find(token);
		if (!s || p.expire(*s, now))
			return SubscriptionResult::Expired;
		if (!current.same_identity(s->metadata.identity)) {
			s->closing = true;
			return SubscriptionResult::IdentityChanged;
		}
		if (!s->window.accept_settled())
			return SubscriptionResult::AwaitSettlement;
		if (s->window.base() && current.revision < s->window.base()->revision) {
			return SubscriptionResult::SourceBehind;
		}
		if (s->window.snapshot())
			return SubscriptionResult::SnapshotReady;
		if (!s->window.base()) {
			s->closing = true;
			return SubscriptionResult::OutOfSync;
		}
		if (!s->window.marker() || !continuity(*s->window.marker(), *s->window.base(), current)) {
			s->closing = true;
			return SubscriptionResult::SnapshotRequired;
		}
		return current.revision == s->window.base()->revision ? SubscriptionResult::Parked
															  : SubscriptionResult::ResumeReady;
	}
	std::optional<Cursor> base(SubscriptionRequestToken token) const noexcept {
		auto *s = impl_->find(token.session);
		return s && s->incarnation == token.incarnation && s->serial == token.request && s->request
				   ? s->window.base()
				   : std::nullopt;
	}
	std::optional<Marker> handoff(SubscriptionRequestToken token) const noexcept {
		auto *s = impl_->find(token.session);
		return s && s->incarnation == token.incarnation && s->serial == token.request && s->request
				   ? s->window.marker()
				   : std::nullopt;
	}
	SubscriptionOfferToken offer(SubscriptionRequestToken token, Cursor target, std::optional<Marker> marker,
								 Clock::time_point now) {
		auto &p = *impl_;
		auto *s = p.find(token);
		if (!s || p.expire(*s, now) || !target.same_identity(s->metadata.identity))
			throw std::logic_error("subscription offer state mismatch");
		const auto window = s->window.offer(target, std::move(marker));
		s->request = false;
		return {s->id, s->incarnation, window};
	}
	void settle(SubscriptionOfferToken token, bool written) noexcept {
		auto *s = impl_->find(token.session);
		if (!s || s->incarnation != token.incarnation || !s->window.settle(token.window))
			return;
		if (!written)
			s->closing = true;
	}
	void release(SubscriptionRequestToken token) noexcept {
		if (auto *s = impl_->find(token)) {
			s->request = false;
		}
	}
	SubscriptionResult close(Identity id, SubscriptionPrincipal principal,
							 std::uint64_t incarnation = 0) noexcept {
		auto *s = impl_->find(id);
		if (!s)
			return SubscriptionResult::Accepted;
		if (incarnation && s->incarnation != incarnation)
			return SubscriptionResult::Expired;
		if (s->metadata.principal.identity != principal.identity)
			return SubscriptionResult::Forbidden;
		if (s->metadata.principal.policy_epoch != principal.policy_epoch)
			return SubscriptionResult::IdentityChanged;
		s->closing = true;
		return SubscriptionResult::Accepted;
	}
	void close_all() noexcept { impl_->closed = true; }
	void collect(std::size_t count, Clock::time_point now) noexcept {
		WorkBudget budget{8192, count};
		collect(budget, now);
	}
	void collect(WorkBudget &budget, Clock::time_point now) noexcept {
		auto &p = *impl_;
		const auto count = std::min(budget.tokens, p.slots.size());
		for (std::size_t i = 0; i < count; ++i) {
			--budget.tokens;
			auto &slot = p.slots[p.cursor];
			const auto ordinal = p.cursor;
			p.cursor = (p.cursor + 1) % p.slots.size();
			if (!slot.session || !p.expire(*slot.session, now) || slot.session->request ||
				slot.session->window.transferring())
				continue;
			if (!slot.retiring) {
				slot.retiring = true;
				auto scope = std::move(slot.session->metadata.scope);
				if (retirement_capacity_ && (slot.retirement_remaining = retirement_capacity_(scope))) {
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
			slot.retiring_scope = Binding{};
			slot.session.reset();
			slot.retiring = false;
			if (slot.incarnation != UINT64_MAX)
				p.free.push_back(ordinal);
		}
	}
	std::size_t size() const noexcept { return impl_->index.size(); }
	bool drained() const noexcept { return impl_->index.empty(); }
};
} // namespace detent::distribution
