#pragma once
#include "paged_graph_validator.hh"
#include "paged_root_prefix.hh"
#include "paged_page_access.hh"
#include "verified_pages.hh"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace detent::local::detail {
inline std::size_t find_segment(std::span<const SegmentInput> segments, const SegmentId &id) {
	auto found =
		std::lower_bound(segments.begin(), segments.end(), id, [](const auto &input, const auto &key) {
			return std::memcmp(input.ownership.segment.data(), key.data(), key.size()) < 0;
		});
	if (found == segments.end() || found->ownership.segment != id)
		return segments.size();
	return found - segments.begin();
}
// The page a reference names in a mapped extent. A page `pages` marks verified needs only the
// reference to match its header's fields; a first touch decodes and compares the header, compares
// the page commitment, runs `first_touch(bytes, header)` and marks the page. Null `pages`: the
// extent was hashed whole, so only the header is compared.
template <class FirstTouch>
std::span<const char> verified_page_bytes(std::span<const char> extent, const VerifiedPages *pages,
										  const paged::Reference &reference, const paged::Limits &limits,
										  FirstTouch &&first_touch) {
	paged::validate(reference);
	if (reference.offset > extent.size() || reference.length > extent.size() - reference.offset)
		throw std::invalid_argument("invalid paged segment reference");
	const auto bytes = extent.subspan(reference.offset, reference.length);
	if (pages && bytes.size() >= paged::page_header_bytes && pages->test(reference.offset)) {
		if (read_integer(bytes, 8, 4) != reference.length || read_integer(bytes, 16, 8) != reference.keys ||
			static_cast<unsigned char>(bytes[24]) != reference.height ||
			std::memcmp(bytes.data() + 32, reference.digest.data(), reference.digest.size()))
			throw ReadSourceFailure("local page reference differs from reached header");
		return bytes;
	}
	const auto header = paged::decode_page_header(bytes, limits);
	if (header.length != reference.length || header.keys != reference.keys ||
		header.height != reference.height || header.digest != reference.digest)
		throw ReadSourceFailure("local page reference differs from reached header");
	if (pages) {
		// First touch compares the page commitment once per exact mapping start.
		try {
			paged::verify_page(bytes, reference, limits);
		} catch (const std::exception &) {
			throw ReadSourceFailure("local page failed first-touch verification");
		}
		first_touch(bytes, header);
		pages->set(reference.offset);
	}
	return bytes;
}
inline std::span<const char> checked_page_bytes(const SegmentInput &segment,
												const paged::Reference &reference) {
	if (segment.ownership.segment != reference.segment)
		throw std::invalid_argument("invalid paged segment reference");
	return verified_page_bytes(segment.bytes, segment.pages, reference, paged_format_limits(),
							   [](std::span<const char>, const paged::PageHeader &) {});
}
} // namespace detent::local::detail
