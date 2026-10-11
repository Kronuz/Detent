#pragma once
#include "sessions.hh"

namespace detent::relay {
struct RegistryBundle {
	distribution::Cursor identity;
	distribution::SubscriptionPrincipal principal;
	std::span<const std::string_view> namespaces;
};
struct ConfiguredCredential {
	std::array<unsigned char, 32> token_hash{};
	SessionAuthorization authorization;
};
// Immutable after startup; the host retains this registry through intake drain.
class Registry {
  public:
	Registry(std::shared_ptr<distribution::StorageLedger>, std::span<const RegistryBundle>,
			 std::span<const ConfiguredCredential>);
	~Registry();
	const SessionAuthorization *authenticate(std::string_view bearer) const noexcept;
	std::optional<std::size_t> resolve(const SessionAuthorization &, const distribution::Cursor &,
									   std::span<const std::string_view>) const noexcept;

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::relay
