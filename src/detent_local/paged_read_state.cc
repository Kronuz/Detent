#include "paged_read_state.hh"
#include <algorithm>
#include <cstring>

namespace detent::local::detail {
std::optional<BorrowedDefinition> PagedReadState::definition(std::string_view ns,
															 std::string_view knob) const {
	if (!root.pages)
		return {};
	std::array<char, 202> storage{};
	const auto size = ns.size() + knob.size() + 3;
	if (size > storage.size())
		return {};
	storage[0] = 'K';
	std::copy(ns.begin(), ns.end(), storage.begin() + 1);
	std::copy(knob.begin(), knob.end(), storage.begin() + ns.size() + 2);
	const std::string_view key(storage.data(), size);
	std::span<const char> identity(root.tree.segment);
	std::size_t offset = root.tree.offset, length = root.tree.length, cursor = 0;
	for (;;) {
		const auto found = std::lower_bound(segments.begin(), segments.end(), identity,
											[](const SegmentInput &input, std::span<const char> value) {
												return std::memcmp(input.ownership.segment.data(),
																   value.data(), value.size()) < 0;
											});
		if (found == segments.end() || std::memcmp(found->ownership.segment.data(), identity.data(), 16))
			throw std::logic_error("validated page dependency missing");
		const auto bytes = found->bytes.subspan(offset, length);
		const auto prefix = read_integer(bytes, 20, 4);
		if (prefix > key.size() - cursor ||
			std::memcmp(bytes.data() + paged::page_header_bytes, key.data() + cursor, prefix))
			return {};
		cursor += prefix;
		const auto kind = static_cast<paged::PageKind>(read_integer(bytes, 12, 4));
		if (kind != paged::PageKind::Branch) {
			if (kind != paged::PageKind::Definition || cursor != key.size())
				return {};
			return BorrowedDefinition::from_paged(bytes.last(read_integer(bytes, 28, 4)));
		}
		if (cursor == key.size())
			return {};
		const auto label = static_cast<unsigned char>(key[cursor++]);
		const auto children = read_integer(bytes, 24, 4);
		const auto begin = paged::page_header_bytes + prefix;
		std::size_t low = 0, high = children;
		while (low < high) {
			const auto middle = low + (high - low) / 2;
			if (static_cast<unsigned char>(bytes[begin + middle * paged::child_bytes]) < label)
				low = middle + 1;
			else
				high = middle;
		}
		if (low == children || static_cast<unsigned char>(bytes[begin + low * paged::child_bytes]) != label)
			return {};
		const auto reference = bytes.subspan(begin + low * paged::child_bytes + 8, paged::reference_bytes);
		identity = reference.first(16);
		offset = read_integer(reference, 16, 4);
		length = read_integer(reference, 20, 4);
	}
}
} // namespace detent::local::detail
