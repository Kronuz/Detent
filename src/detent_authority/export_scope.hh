#pragma once
#include "detent/cursor.hh"
#include "detent_distribution/allocator.hh"
#include "detent_distribution/rows.hh"
#include <set>
#include <span>
#include <string>

namespace detent::authority {
// Minted by authenticated request admission from a trusted policy identity.
// Credential secrets never enter the canonical scope identity.
class AuthorizedScope {
	struct FactoryTag {
		explicit FactoryTag() = default;
	};

  public:
	AuthorizedScope(FactoryTag, std::shared_ptr<distribution::StorageLedger> account)
		: account_(std::move(account)), arena_(*account_), names_(arena_) {}
	AuthorizedScope(const AuthorizedScope &) = delete;
	const distribution::ScopeIdentity &identity() const noexcept { return identity_; }
	std::uint64_t size() const noexcept { return names_.size(); }
	std::string_view name(std::uint64_t ordinal) const { return names_[ordinal]; }
	std::shared_ptr<distribution::StorageLedger> storage_account() const noexcept { return account_; }
	static std::shared_ptr<const AuthorizedScope>
	authorize(std::string_view principal, std::uint64_t policy_epoch, std::span<const std::string> requested,
			  const std::set<std::string> &allowed, std::shared_ptr<distribution::StorageLedger> account,
			  std::size_t maximum_names = 4096);

  private:
	std::shared_ptr<distribution::StorageLedger> account_;
	distribution::PagedStorage arena_;
	distribution::StableRows<std::string_view> names_;
	distribution::ScopeIdentity identity_{};
};
} // namespace detent::authority
