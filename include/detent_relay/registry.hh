#pragma once
#include "detent_distribution/grants.hh"
#include "detent_distribution/session_table.hh"

namespace detent::relay {
// A downstream credential: its token's SHA-256, the principal that owns its sessions and the W13
// namespace patterns it grants.
struct ConfiguredCredential {
	std::array<unsigned char, 32> token_hash{};
	distribution::SubscriptionPrincipal principal;
	std::shared_ptr<const distribution::NamespaceGrants> namespaces;
};
// Immutable after startup; the host retains this registry through intake drain.
class Registry {
  public:
	Registry(std::shared_ptr<distribution::StorageLedger>, std::span<const ConfiguredCredential>);
	~Registry();
	const ConfiguredCredential *authenticate(std::string_view bearer) const noexcept;

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::relay
