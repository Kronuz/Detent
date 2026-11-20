#include "borrowed_definition.hh"

namespace detent::local::detail {
BorrowedDefinition BorrowedDefinition::from_paged(std::span<const char> body) {
	return {body, paged::decode_definition_header(body, UINT64_MAX)};
}
std::int64_t BorrowedDefinition::default_value() const { return paged_header.default_value; }
std::optional<std::uint8_t> BorrowedDefinition::rollout() const { return paged_header.rollout; }
std::int64_t BorrowedDefinition::rollout_value() const {
	return paged_header.rollout_value.value_or(paged_header.default_value);
}
std::optional<GroupOverride> BorrowedDefinition::override_for(std::string_view key) const {
	std::size_t offset = paged::header_size(paged_header);
	for (std::uint32_t i = 0; i < paged_header.overrides; ++i) {
		const auto row = paged::read_override(record, offset);
		if (row.key == key)
			return row.value;
		if (row.key > key)
			return {};
	}
	return {};
}
} // namespace detent::local::detail
