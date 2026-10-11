#include "detent_relay/registry.hh"
#include "detent/validation.hh"
#include <cstring>
#include <sha256.h>

namespace detent::relay {
namespace d = distribution;
struct Registry::Impl {
	struct Binding {
		d::Cursor identity;
		d::SubscriptionPrincipal principal;
		std::span<const std::string_view> namespaces;
	};
	std::shared_ptr<d::StorageLedger> account;
	d::PagedStorage arena;
	std::vector<Binding, d::StorageAllocator<Binding>> bindings;
	std::vector<ConfiguredCredential, d::StorageAllocator<ConfiguredCredential>> credentials;
	Impl(std::shared_ptr<d::StorageLedger> ledger, std::span<const RegistryBundle> configured,
		 std::span<const ConfiguredCredential> authorized)
		: account(std::move(ledger)), arena(*account, 4096), bindings(d::StorageAllocator<Binding>(account)),
		  credentials(d::StorageAllocator<ConfiguredCredential>(account)) {
		if (configured.empty() || configured.size() > 256 || authorized.empty() || authorized.size() > 10000)
			throw std::invalid_argument("relay registry capacity");
		bindings.reserve(configured.size());
		for (const auto &value : configured) {
			if (value.identity.cluster == d::Identity{} || value.identity.group == d::Identity{} ||
				value.identity.restore_epoch == d::Identity{} || value.identity.scope == d::ScopeIdentity{} ||
				value.principal.identity == d::ScopeIdentity{} || !value.principal.policy_epoch ||
				value.namespaces.size() > 4096)
				throw std::invalid_argument("invalid relay registry binding");
			for (const auto &prior : bindings)
				if (prior.identity.same_identity(value.identity))
					throw std::invalid_argument("duplicate relay registry binding");
			auto *names = static_cast<std::string_view *>(arena.allocate(value.namespaces.size_bytes()));
			std::string_view prior;
			std::size_t index = 0;
			for (auto name : value.namespaces) {
				if (name.empty() || name.size() > 198 || (!prior.empty() && prior >= name) ||
					!std::all_of(name.begin(), name.end(), detent::detail::is_name_char))
					throw std::invalid_argument("noncanonical relay registry namespaces");
				auto *copied = static_cast<char *>(arena.allocate(name.size(), 1));
				std::memcpy(copied, name.data(), name.size());
				std::construct_at(names + index++, copied, name.size());
				prior = name;
			}
			bindings.push_back({value.identity, value.principal, {names, index}});
		}
		credentials.reserve(authorized.size());
		for (const auto &credential : authorized) {
			bool granted = false;
			for (std::size_t index = 0; index < 256; ++index)
				if (credential.authorization.allows(index)) {
					if (index >= bindings.size() ||
						credential.authorization.principal != bindings[index].principal)
						throw std::invalid_argument("relay registry credential binding mismatch");
					granted = true;
				}
			if (!granted || credential.authorization.grants == d::ScopeIdentity{})
				throw std::invalid_argument("empty relay registry grant");
			credentials.push_back(credential);
		}
		std::sort(credentials.begin(), credentials.end(),
				  [](const auto &a, const auto &b) { return a.token_hash < b.token_hash; });
		for (std::size_t index = 1; index < credentials.size(); ++index)
			if (credentials[index - 1].token_hash == credentials[index].token_hash)
				throw std::invalid_argument("duplicate relay registry credential");
	}
};
Registry::Registry(std::shared_ptr<d::StorageLedger> account, std::span<const RegistryBundle> bundles,
				   std::span<const ConfiguredCredential> credentials) {
	if (!account)
		throw std::invalid_argument("relay registry storage account");
	impl_ = d::make_storage_owner<Impl>(account, std::malloc, std::free, account, bundles, credentials);
}
Registry::~Registry() = default;
const SessionAuthorization *Registry::authenticate(std::string_view bearer) const noexcept {
	if (!bearer.starts_with("Bearer ") || bearer.size() < 39 || bearer.size() > 135)
		return nullptr;
	auto token = bearer.substr(7);
	class SHA256 hash;
	hash.add(token.data(), token.size());
	std::array<unsigned char, 32> digest;
	hash.getHash(digest.data());
	const auto &credentials = impl_->credentials;
	const auto found = std::lower_bound(credentials.begin(), credentials.end(), digest,
										[](const ConfiguredCredential &candidate, const auto &value) {
											return candidate.token_hash < value;
										});
	if (found == credentials.end())
		return nullptr;
	unsigned difference = 0;
	for (std::size_t index = 0; index < digest.size(); ++index)
		difference |= digest[index] ^ found->token_hash[index];
	return difference ? nullptr : &found->authorization;
}
std::optional<std::size_t> Registry::resolve(const SessionAuthorization &authorization,
											 const d::Cursor &identity,
											 std::span<const std::string_view> names) const noexcept {
	for (std::size_t index = 0; index < impl_->bindings.size(); ++index) {
		const auto &binding = impl_->bindings[index];
		if (authorization.allows(index) && authorization.principal == binding.principal &&
			identity.same_identity(binding.identity) &&
			std::equal(names.begin(), names.end(), binding.namespaces.begin(), binding.namespaces.end()))
			return index;
	}
	return {};
}
} // namespace detent::relay
