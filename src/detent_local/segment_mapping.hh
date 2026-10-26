#pragma once
#include "detent_distribution/retirement.hh"
#include "detent_local/ownership.hh"
#include <optional>
#include <stdexcept>
#include <sys/types.h>

namespace detent::local::detail {
struct SegmentFileIdentity {
	std::uint64_t device = 0, inode = 0;
	bool operator==(const SegmentFileIdentity &) const = default;
};
struct SegmentExtent {
	std::size_t length;
};
// Worker-side mapping admission; graph validation still establishes trust.
class SegmentMapping final : public distribution::RetirementNode {
  public:
	SegmentMapping(std::shared_ptr<distribution::StorageLedger>, int fd, uid_t owner, SegmentOwnership);
	SegmentMapping(std::shared_ptr<distribution::StorageLedger>, int fd, uid_t owner, SegmentExtent);
	SegmentMapping(std::shared_ptr<distribution::StorageLedger>, SegmentOwnership);
	void install(int fd, uid_t owner);
	std::span<const char> bytes() const {
		if (!mapped_)
			throw std::logic_error("segment mapping is not installed");
		return {address_, length_};
	}
	bool has_ownership() const noexcept { return ownership_.has_value(); }
	const SegmentOwnership &ownership() const {
		if (!ownership_)
			throw std::logic_error("extent-only mapping has no ownership evidence");
		return *ownership_;
	}
	const std::shared_ptr<distribution::StorageLedger> &storage_account() const noexcept { return account_; }
	SegmentFileIdentity identity() const noexcept { return identity_; }

  protected:
	bool retire_step() noexcept override;
	bool retry_later() const noexcept override { return retry_; }

  private:
	std::shared_ptr<distribution::StorageLedger> account_;
	std::optional<SegmentOwnership> ownership_;
	std::size_t length_;
	SegmentFileIdentity identity_;
	distribution::StorageLedger::Ticket ticket_;
	char *address_ = nullptr;
	bool mapped_ = false, retry_ = false;
	void install_checked(int, SegmentFileIdentity);
};
} // namespace detent::local::detail
