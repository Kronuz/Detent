#pragma once
#include "bundle.hh"
#include "detent_distribution/session_table.hh"

namespace detent::relay {
struct SessionBinding {
	std::shared_ptr<Bundle> bundle;
	distribution::SubscriptionPrincipal principal;
	distribution::Cursor identity;
};
struct SessionAuthorization {
	distribution::SubscriptionPrincipal principal;
	distribution::ScopeIdentity grants{};
	std::array<std::uint64_t, 4> bundles{};
	bool allows(std::size_t index) const noexcept {
		return index < 256 && (bundles[index / 64] & (std::uint64_t{1} << (index % 64)));
	}
};
struct PreparedOffer {
	distribution::SubscriptionResult result;
	std::optional<distribution::SubscriptionOfferToken> token;
	distribution::RetirementHandle<distribution::EncodedUpdate> update;
};
// The trusted intake resolves exact namespaces/profile to a finite binding.
// Authorization masks come from immutable credential/grant configuration.
class Sessions {
  public:
	using Clock = std::chrono::steady_clock;
	using Metadata = distribution::SessionMetadata<std::size_t>;
	Sessions(std::shared_ptr<distribution::StorageLedger>, std::span<const SessionBinding>,
			 distribution::SubscriptionLimits = {});
	~Sessions();
	distribution::SubscriptionResult open(distribution::Identity, const SessionAuthorization &,
										  std::size_t binding, std::optional<distribution::Cursor>,
										  bool force_snapshot, bool compression, Clock::time_point);
	const Metadata *metadata(distribution::Identity, Clock::time_point) noexcept;
	distribution::SubscriptionAdmission next(distribution::Identity, const SessionAuthorization &,
											 std::uint64_t after, std::optional<distribution::Cursor>,
											 Clock::time_point);
	PreparedOffer prepare(distribution::SubscriptionRequestToken, Clock::time_point);
	void settle(distribution::SubscriptionOfferToken, bool written) noexcept;
	void release(distribution::SubscriptionRequestToken) noexcept;
	distribution::SubscriptionResult close(distribution::Identity, const SessionAuthorization &,
										   Clock::time_point) noexcept;
	void close_all() noexcept;
	void collect(distribution::WorkBudget &, Clock::time_point) noexcept;
	std::size_t size() const noexcept;
	bool drained() const noexcept;

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::relay
