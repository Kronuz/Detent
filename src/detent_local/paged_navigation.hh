#pragma once
#include "paged_graph_validator.hh"
#include "paged_root_prefix.hh"
#include "paged_page_access.hh"
#include "verified_pages.hh"
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
	paged::validate(reference);
	if (segment.ownership.segment != reference.segment || reference.offset > segment.bytes.size() ||
		reference.length > segment.bytes.size() - reference.offset)
		throw std::invalid_argument("invalid paged segment reference");
	const auto bytes = segment.bytes.subspan(reference.offset, reference.length);
	const auto header = paged::decode_page_header(bytes, paged_format_limits());
	if (header.length != reference.length || header.keys != reference.keys ||
		header.height != reference.height || header.digest != reference.digest)
		throw ReadSourceFailure("local page reference differs from reached header");
	if (segment.pages && !segment.pages->test(reference.offset)) {
		// First touch compares the page commitment once per exact mapping start.
		try {
			paged::verify_page(bytes, reference, paged_format_limits());
		} catch (const std::exception &) {
			throw ReadSourceFailure("local page failed first-touch verification");
		}
		segment.pages->set(reference.offset);
	}
	return bytes;
}
} // namespace detent::local::detail
