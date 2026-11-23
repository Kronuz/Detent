#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace detent::distribution {
// W13: the namespaces a credential grants. A pattern is an exact name, a whole-component
// prefix `p.*` (every name below `p.`, never `p` itself), or `*`; any other form is refused.
class NamespaceGrants {
  public:
	static constexpr std::size_t maximum_patterns = 4096;
	NamespaceGrants() = default; // grants nothing
	explicit NamespaceGrants(std::span<const std::string_view> patterns);
	bool allows(std::string_view name) const noexcept;
	bool all() const noexcept { return all_; }
	bool empty() const noexcept { return !all_ && exact_.empty() && prefixes_.empty(); }

  private:
	std::vector<std::string> exact_, prefixes_; // sorted, unique; prefixes without the ".*"
	bool all_ = false;
};
// A namespace name as the authority accepts one: 1 to 198 name characters.
bool valid_namespace_name(std::string_view) noexcept;
} // namespace detent::distribution
