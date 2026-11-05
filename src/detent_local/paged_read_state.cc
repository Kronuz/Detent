#include "paged_read_state.hh"
#include "paged_navigation.hh"
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
	const auto body = lookup(key, paged::PageKind::Definition);
	if (!body)
		return {};
	if (access) {
		storage[0] = 'W';
		const auto watermark = lookup({storage.data(), ns.size() + 2}, paged::PageKind::Namespace);
		if (!watermark || watermark->size() != 8)
			throw ReadSourceFailure("definition namespace watermark is missing");
		const auto revision = read_integer(*watermark, 0, 8);
		if (revision > root.cursor.revision)
			throw ReadSourceFailure("namespace watermark exceeds selected revision");
		try {
			paged::decode_definition_header(*body, revision, access->maximum_overrides());
		} catch (const std::invalid_argument &) {
			throw ReadSourceFailure("definition conflicts with its namespace watermark");
		}
	}
	return BorrowedDefinition::from_paged(*body);
}
std::optional<std::span<const char>> PagedReadState::lookup(std::string_view key,
                                                            paged::PageKind expected) const {
	if (!root.pages)
		return {};
	std::span<const char> encoded;
	std::size_t cursor = 0;
	for (;;) {
		std::span<const char> bytes;
		const auto reference = encoded.empty() ? root.tree : paged::decode_reference(encoded);
		if (access) {
			if (!access->available())
				throw ReadSourceFailure("local generation is unavailable");
			bytes = access->resolve(reference);
		} else {
			// The first-touch accessor checks a page's digests the first time any read reaches it.
			const auto found = find_segment(segments, reference.segment);
			if (found == segments.size())
				throw ReadSourceFailure("local page names a segment outside its generation");
			try {
				bytes = checked_page_bytes(segments[found], reference);
			} catch (const std::invalid_argument &) {
				throw ReadSourceFailure("local page exceeds its segment");
			}
		}
		const auto prefix = read_integer(bytes, 20, 4);
		const auto kind = static_cast<paged::PageKind>(read_integer(bytes, 12, 4));
		if (access && kind != paged::PageKind::Branch) {
			std::array<char, 202> actual{};
			if (cursor > actual.size() || prefix > actual.size() - cursor)
				throw ReadSourceFailure("local leaf key exceeds format bounds");
			std::copy_n(key.data(), cursor, actual.data());
			std::copy_n(bytes.data() + paged::page_header_bytes, prefix, actual.data() + cursor);
			try {
				if (paged::decode_key({actual.data(), cursor + prefix}).kind != kind)
					throw ReadSourceFailure("local leaf has an unexpected key type");
			} catch (const std::invalid_argument &) {
				throw ReadSourceFailure("local leaf key is invalid");
			}
		}
		if (prefix > key.size() - cursor ||
		    std::memcmp(bytes.data() + paged::page_header_bytes, key.data() + cursor, prefix))
			return {};
		cursor += prefix;
		if (kind != paged::PageKind::Branch) {
			if (cursor != key.size())
				return {};
			if (kind != expected)
				throw ReadSourceFailure("local page has an unexpected key type");
			return bytes.last(read_integer(bytes, 28, 4));
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
		encoded = bytes.subspan(begin + low * paged::child_bytes + 8, paged::reference_bytes);
	}
}
} // namespace detent::local::detail
