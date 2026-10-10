#pragma once
#include "delivery_export.hh"
#include <chrono>

namespace detent::authority {
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
	Full
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
struct SubscriptionMetadata {
	SubscriptionPrincipal principal;
	std::shared_ptr<const AuthorizedScope> scope;
	distribution::Cursor identity;
	bool compression = false;
	std::uint64_t incarnation = 0;
	std::optional<distribution::ScopeIdentity> creator_grants, last_authorized_grants;
};
// Owner-executor table. Release tokens only after accepted work and originals drain.
class Subscriptions {
  public:
	using Clock = std::chrono::steady_clock;
	Subscriptions(std::shared_ptr<distribution::StorageLedger>, SubscriptionLimits = {});
	~Subscriptions();
	SubscriptionResult open(distribution::Identity, SubscriptionPrincipal,
							std::shared_ptr<const AuthorizedScope>, distribution::Cursor current,
							std::optional<distribution::Cursor> base, bool force_snapshot, bool compression,
							ChangeHistory &, Clock::time_point,
							std::optional<distribution::ScopeIdentity> grants = {});
	const SubscriptionMetadata *metadata(distribution::Identity, Clock::time_point) noexcept;
	SubscriptionAdmission next(distribution::Identity, SubscriptionPrincipal, std::uint64_t after,
							   std::optional<distribution::Cursor> published, Clock::time_point,
							   std::uint64_t incarnation = 0);
	SubscriptionResult authorize(distribution::Identity, std::uint64_t incarnation, SubscriptionPrincipal,
								 distribution::ScopeIdentity scope, distribution::ScopeIdentity grants,
								 Clock::time_point) noexcept;
	SubscriptionResult poll(SubscriptionRequestToken, distribution::Cursor current, ChangeHistory &,
							Clock::time_point) noexcept;
	std::optional<distribution::Cursor> base(SubscriptionRequestToken) const noexcept;
	std::optional<ChangeHistory::Handoff> handoff(SubscriptionRequestToken) const noexcept;
	SubscriptionOfferToken offer(SubscriptionRequestToken, distribution::Cursor,
								 std::optional<ChangeHistory::Handoff>, Clock::time_point);
	void settle(SubscriptionOfferToken, bool written) noexcept;
	void release(SubscriptionRequestToken) noexcept;
	SubscriptionResult close(distribution::Identity, SubscriptionPrincipal,
							 std::uint64_t incarnation = 0) noexcept;
	void close_all() noexcept;
	void collect(std::size_t slots, Clock::time_point) noexcept;
	void collect(distribution::WorkBudget &, Clock::time_point) noexcept;
	std::size_t size() const noexcept;
	bool drained() const noexcept;

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::authority
