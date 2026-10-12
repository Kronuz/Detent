#pragma once
#include "detent_local/ownership.hh"
#include <algorithm>
#include <optional>
#include <string_view>
namespace detent::local::detail {
enum class SegmentFile { Owner, Payload };
struct NamedSegment {
	StorageIdentity segment;
	SegmentFile kind;
};
// Canonical spelling identifies census entries, never ownership or deletion authority.
inline std::optional<NamedSegment> parse_segment_name(std::string_view name) noexcept {
	constexpr std::string_view prefix = "segment-";
	constexpr auto base_length = prefix.size() + 32;
	if (!name.starts_with(prefix) || name.size() < base_length)
		return {};
	const auto ending = name.substr(base_length);
	SegmentFile kind;
	if (ending == ".owner")
		kind = SegmentFile::Owner;
	else if (ending == ".pack")
		kind = SegmentFile::Payload;
	else
		return {};
	NamedSegment parsed{{}, kind};
	auto digit = [](unsigned char value) -> int {
		if (value >= '0' && value <= '9')
			return value - '0';
		if (value >= 'a' && value <= 'f')
			return value - 'a' + 10;
		return -1;
	};
	for (std::size_t i = 0; i < parsed.segment.size(); ++i) {
		const auto high = digit(name[prefix.size() + i * 2]);
		const auto low = digit(name[prefix.size() + i * 2 + 1]);
		if (high < 0 || low < 0)
			return {};
		parsed.segment[i] = static_cast<char>((high << 4) | low);
	}
	return parsed;
}
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
