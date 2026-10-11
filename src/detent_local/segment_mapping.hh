#pragma once
#include "detent_distribution/retirement.hh"
#include "detent_local/ownership.hh"
#include <sys/types.h>

namespace detent::local::detail {
struct SegmentFileIdentity {
	std::uint64_t device = 0, inode = 0;
	bool operator==(const SegmentFileIdentity &) const = default;
};
// Worker-side mapping admission; graph validation still establishes trust.
class SegmentMapping final : public distribution::RetirementNode {
  public:
	SegmentMapping(std::shared_ptr<distribution::StorageLedger>, int fd, uid_t owner, SegmentOwnership);
	std::span<const char> bytes() const noexcept { return {address_, ownership_.length}; }
	const SegmentOwnership &ownership() const noexcept { return ownership_; }
	const std::shared_ptr<distribution::StorageLedger> &storage_account() const noexcept { return account_; }
	SegmentFileIdentity identity() const noexcept { return identity_; }

  protected:
	bool retire_step() noexcept override;
	bool retry_later() const noexcept override { return retry_; }

  private:
	std::shared_ptr<distribution::StorageLedger> account_;
	SegmentOwnership ownership_;
	SegmentFileIdentity identity_;
	distribution::StorageLedger::Ticket ticket_;
	char *address_ = nullptr;
	bool mapped_ = false, retry_ = false;
};
} // namespace detent::local::detail
