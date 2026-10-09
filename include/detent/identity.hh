#pragma once
#include <array>
#include <stdexcept>
#include <string_view>

namespace detent::distribution {
// Canonical controller representation shared by authority/agent/reader tools.
template <std::size_t N> std::array<char, N> parse_identity_hex(std::string_view input) {
	if (input.size() != N * 2)
		throw std::invalid_argument("identity hex length");
	auto nibble = [](char c) -> unsigned {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		throw std::invalid_argument("identity requires lowercase hex");
	};
	std::array<char, N> result{};
	for (std::size_t i = 0; i < N; ++i)
		result[i] = static_cast<char>((nibble(input[i * 2]) << 4) | nibble(input[i * 2 + 1]));
	if (result == std::array<char, N>{})
		throw std::invalid_argument("zero identity");
	return result;
}
} // namespace detent::distribution
