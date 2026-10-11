#pragma once
#include "paged_graph_validator.hh"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace detent::local::detail {
inline std::size_t find_segment(std::span<const SegmentInput> segments, const StorageIdentity &id) {
	auto found =
		std::lower_bound(segments.begin(), segments.end(), id, [](const auto &input, const auto &key) {
			return std::memcmp(input.ownership.segment.data(), key.data(), key.size()) < 0;
		});
	if (found == segments.end() || found->ownership.segment != id)
		return segments.size();
	return found - segments.begin();
}
inline std::span<const char> checked_page_bytes(const SegmentInput &segment,
												const paged::Reference &reference) {
	paged::encode_reference(reference);
	if (segment.ownership.segment != reference.segment || reference.offset > segment.bytes.size() ||
		reference.length > segment.bytes.size() - reference.offset)
		throw std::invalid_argument("invalid paged segment reference");
	return segment.bytes.subspan(reference.offset, reference.length);
}
} // namespace detent::local::detail
