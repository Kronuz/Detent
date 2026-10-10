#include "export_scope.hh"
#include "detent/scope.hh"
#include <algorithm>
#include <vector>
namespace detent::authority {
std::shared_ptr<const AuthorizedScope>
AuthorizedScope::authorize(std::string_view principal, std::uint64_t policy_epoch,
						   std::span<const std::string> requested, const std::set<std::string> &allowed,
						   std::shared_ptr<distribution::StorageLedger> account, std::size_t maximum_names) {
	if (!account || principal.empty() || principal.size() > 128 || !policy_epoch || maximum_names > 4096 ||
		requested.size() > maximum_names)
		throw std::invalid_argument("invalid authorized scope admission");
	using Names = std::vector<std::string_view, distribution::StorageAllocator<std::string_view>>;
	Names canonical{distribution::StorageAllocator<std::string_view>(account)};
	canonical.reserve(requested.size());
	for (const auto &name : requested) {
		if (name.empty() || name.size() > 198 ||
			!std::all_of(name.begin(), name.end(), detent::detail::is_name_char))
			throw std::invalid_argument("invalid requested namespace");
		if (!allowed.contains(name))
			throw std::invalid_argument("unauthorized requested namespace");
		canonical.push_back(name);
	}
	std::sort(canonical.begin(), canonical.end());
	canonical.erase(std::unique(canonical.begin(), canonical.end()), canonical.end());
	return authorize_canonical(principal, policy_epoch, canonical, canonical, std::move(account),
							   maximum_names);
}
std::shared_ptr<const AuthorizedScope> AuthorizedScope::authorize_canonical(
	std::string_view principal, std::uint64_t policy_epoch, std::span<const std::string_view> requested,
	std::span<const std::string_view> allowed, std::shared_ptr<distribution::StorageLedger> account,
	std::size_t maximum_names) {
	if (!account || maximum_names > 4096 || requested.size() > maximum_names || allowed.size() > 4096 ||
		std::adjacent_find(allowed.begin(), allowed.end(), std::greater_equal<>()) != allowed.end())
		throw std::invalid_argument("invalid canonical scope admission");
	const auto identity = distribution::canonical_scope_identity(principal, policy_epoch, requested);
	for (auto name : requested)
		if (!std::binary_search(allowed.begin(), allowed.end(), name))
			throw std::invalid_argument("unauthorized requested namespace");
	auto result = std::allocate_shared<AuthorizedScope>(
		distribution::StorageAllocator<AuthorizedScope>(account), FactoryTag{}, account);
	result->identity_ = identity;
	for (auto name : requested)
		result->names_.push_back(distribution::store_text(result->arena_, name).value);
	return result;
}
} // namespace detent::authority
