#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_distribution/protocol.hh"

namespace detent::authority {
enum class SubscriptionRequestKind { Open, Next, Close };
struct SubscriptionRequestLimits {
	std::size_t opens = 16, polls = 256, closes = 16;
	std::size_t per_principal_controls = 8, per_principal_polls = 256;
	std::size_t open_bytes = 256 * 1024, control_bytes = 4096;
};
// Thread-safe admission. A retained lease covers the body and accepted originals.
class SubscriptionRequests {
	struct Impl;

  public:
	class Lease {
	  public:
		Lease(std::shared_ptr<Impl>, distribution::ScopeIdentity, SubscriptionRequestKind,
			  std::shared_ptr<distribution::OwnedBuffer>);
		~Lease();
		Lease(const Lease &) = delete;
		Lease &operator=(const Lease &) = delete;
		distribution::OwnedBuffer &body() noexcept { return *body_; }

	  private:
		friend class SubscriptionRequests;
		std::shared_ptr<Impl> owner_;
		distribution::ScopeIdentity principal_;
		SubscriptionRequestKind kind_;
		std::shared_ptr<distribution::OwnedBuffer> body_;
		bool active_ = false;
	};
	SubscriptionRequests(std::shared_ptr<distribution::StorageLedger>, SubscriptionRequestLimits = {});
	std::shared_ptr<Lease> admit(distribution::ScopeIdentity, SubscriptionRequestKind);
	void close() noexcept;
	bool drained() const noexcept;
	std::size_t body_limit(SubscriptionRequestKind) const noexcept;

  private:
	std::shared_ptr<Impl> impl_;
};
} // namespace detent::authority
