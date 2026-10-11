#pragma once
#include "detent_local/ownership.hh"
#include <algorithm>
#include <string_view>
namespace detent::local::detail {
struct SegmentNames {
	std::array<char, 47> ownership{};
	std::array<char, 46> payload{};
	explicit SegmentNames(const StorageIdentity &id) {
		constexpr std::string_view prefix = "segment-";
		constexpr char hex[] = "0123456789abcdef";
		std::copy(prefix.begin(), prefix.end(), ownership.begin());
		std::size_t offset = prefix.size();
		for (unsigned char byte : id) {
			ownership[offset++] = hex[byte >> 4];
			ownership[offset++] = hex[byte & 15];
		}
		std::copy_n(ownership.begin(), offset, payload.begin());
		constexpr std::string_view owner_suffix = ".owner", payload_suffix = ".pack";
		std::copy(owner_suffix.begin(), owner_suffix.end(), ownership.begin() + offset);
		std::copy(payload_suffix.begin(), payload_suffix.end(), payload.begin() + offset);
	}
	std::string_view owner_name() const noexcept { return {ownership.data(), ownership.size() - 1}; }
	std::string_view payload_name() const noexcept { return {payload.data(), payload.size() - 1}; }
};
} // namespace detent::local::detail
