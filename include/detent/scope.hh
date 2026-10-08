#pragma once
#include "cursor.hh"
#include "validation.hh"
#include <algorithm>
#include <sha256.h>
#include <span>
#include <stdexcept>
#include <string_view>

namespace detent::distribution {
// Shared authority/agent identity calculation. The trusted controller supplies
// principal and policy epoch; callers canonicalize and authorize names first.
inline ScopeIdentity canonical_scope_identity(std::string_view principal, std::uint64_t policy_epoch,
											  std::span<const std::string_view> names) {
	if (principal.empty() || principal.size() > 128 || !policy_epoch || names.size() > 4096)
		throw std::invalid_argument("invalid scope identity context");
	SHA256 hash;
	constexpr std::string_view domain = "detent-authorized-scope-v1";
	hash.add(domain.data(), domain.size());
	auto number = [&](std::uint64_t value) {
		std::array<unsigned char, 8> bytes{};
		for (unsigned i = 0; i < 8; ++i)
			bytes[7 - i] = static_cast<unsigned char>(value >> (i * 8));
		hash.add(bytes.data(), bytes.size());
	};
	auto text = [&](std::string_view value) {
		number(value.size());
		hash.add(value.data(), value.size());
	};
	text(principal);
	number(policy_epoch);
	number(names.size());
	std::string_view previous;
	for (auto name : names) {
		if (name.empty() || name.size() > 198 || (!previous.empty() && previous >= name) ||
			!std::all_of(name.begin(), name.end(), detent::detail::is_name_char))
			throw std::invalid_argument("scope names are not canonical");
		text(name);
		previous = name;
	}
	unsigned char digest[32];
	hash.getHash(digest);
	ScopeIdentity result;
	std::copy_n(reinterpret_cast<const char *>(digest), result.size(), result.begin());
	return result;
}
} // namespace detent::distribution
