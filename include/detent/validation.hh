#pragma once

// Section 4: name validation. `^[A-Za-z0-9._/-]+:[A-Za-z0-9._/-]+$`, exactly
// one colon, 1-200 UTF-8 bytes. Group keys are opaque, case-sensitive UTF-8
// strings, 1-128 bytes (validated only for byte-length here; no charset
// restriction is specified for group keys in the spec).

#include <chars.hh>

#include <string_view>

namespace detent {

namespace detail {

[[nodiscard]] constexpr bool is_name_char(char c) noexcept {
	return chars::is_alnum(c) != 0 || c == '.' || c == '_' || c == '/' || c == '-';
}

} // namespace detail

[[nodiscard]] constexpr bool is_valid_knob_name(std::string_view name) noexcept {
	if (name.empty() || name.size() > 200) return false;
	auto colon = name.find(':');
	if (colon == std::string_view::npos) return false;
	if (name.find(':', colon + 1) != std::string_view::npos) return false; // exactly one colon
	auto knobset = name.substr(0, colon);
	auto local = name.substr(colon + 1);
	if (knobset.empty() || local.empty()) return false;
	for (char c : knobset) if (!detail::is_name_char(c)) return false;
	for (char c : local) if (!detail::is_name_char(c)) return false;
	return true;
}

[[nodiscard]] constexpr bool is_valid_group_key(std::string_view key) noexcept {
	return !key.empty() && key.size() <= 128;
}

} // namespace detent
