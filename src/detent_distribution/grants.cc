#include "detent_distribution/grants.hh"
#include "detent/validation.hh"
#include <algorithm>
#include <stdexcept>

namespace detent::distribution {
bool valid_namespace_name(std::string_view name) noexcept {
	return !name.empty() && name.size() <= 198 && std::all_of(name.begin(), name.end(), detail::is_name_char);
}
NamespaceGrants::NamespaceGrants(std::span<const std::string_view> patterns) {
	if (patterns.size() > maximum_patterns)
		throw std::invalid_argument("namespace grant count bound");
	for (auto pattern : patterns) {
		if (pattern == "*")
			all_ = true;
		else if (pattern.size() > 2 && pattern.ends_with(".*") &&
				 valid_namespace_name(pattern.substr(0, pattern.size() - 2)))
			prefixes_.emplace_back(pattern.substr(0, pattern.size() - 2));
		else if (valid_namespace_name(pattern))
			exact_.emplace_back(pattern);
		else
			throw std::invalid_argument("namespace grant is not a name, a `prefix.*` or `*`");
	}
	for (auto *names : {&exact_, &prefixes_}) {
		std::sort(names->begin(), names->end());
		names->erase(std::unique(names->begin(), names->end()), names->end());
	}
}
bool NamespaceGrants::allows(std::string_view name) const noexcept {
	if (!valid_namespace_name(name))
		return false;
	if (all_ || std::binary_search(exact_.begin(), exact_.end(), name, std::less<>{}))
		return true;
	if (prefixes_.empty())
		return false;
	// Each '.' ends a whole-component prefix the name lies below.
	for (auto dot = name.find('.'); dot != std::string_view::npos; dot = name.find('.', dot + 1))
		if (std::binary_search(prefixes_.begin(), prefixes_.end(), name.substr(0, dot), std::less<>{}))
			return true;
	return false;
}
} // namespace detent::distribution
