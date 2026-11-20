#include "detent_relay/registry.hh"
#include <algorithm>
#include <sha256.h>

namespace detent::relay {
namespace d = distribution;
struct Registry::Impl {
	std::vector<ConfiguredCredential, d::StorageAllocator<ConfiguredCredential>> credentials;
	Impl(std::shared_ptr<d::StorageLedger> account, std::span<const ConfiguredCredential> authorized)
		: credentials(d::StorageAllocator<ConfiguredCredential>(account)) {
		if (authorized.empty() || authorized.size() > 10000)
			throw std::invalid_argument("relay registry capacity");
		credentials.reserve(authorized.size());
		for (const auto &credential : authorized) {
			if (credential.principal.identity == d::ScopeIdentity{} || !credential.principal.policy_epoch ||
				!credential.namespaces || credential.namespaces->empty())
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
Registry::Registry(std::shared_ptr<d::StorageLedger> account, std::span<const ConfiguredCredential> credentials) {
	if (!account)
		throw std::invalid_argument("relay registry storage account");
	impl_ = d::make_storage_owner<Impl>(account, std::malloc, std::free, account, credentials);
}
Registry::~Registry() = default;
const ConfiguredCredential *Registry::authenticate(std::string_view bearer) const noexcept {
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
	return difference ? nullptr : &*found;
}
} // namespace detent::relay
