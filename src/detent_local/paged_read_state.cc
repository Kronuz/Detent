#include "paged_read_state.hh"
#include "paged_navigation.hh"
#include "paged_node_step.hh"
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
	try {
		auto reference = root.tree;
		std::span<const char> bytes;
		std::size_t cursor = 0;
		paged::NodeAt at;
		bool resolve = true;
		for (std::size_t visits = 0; visits <= key.size(); ++visits) {
			if (resolve) {
				if (access) {
					if (!access->available())
						throw ReadSourceFailure("local generation is unavailable");
					bytes = access->resolve(reference);
				} else {
					const auto found = find_segment(segments, reference.segment);
					if (found == segments.size())
						throw ReadSourceFailure("local page names a missing segment");
					bytes = checked_page_bytes(segments[found], reference);
				}
				resolve = false;
				at = {};
			}
			const auto node = at.decode(bytes, paged_format_limits());
			if (node.kind != paged::PageKind::Branch) {
				std::array<char, 202> actual{};
				if (cursor > actual.size() || node.prefix.size() > actual.size() - cursor)
					throw ReadSourceFailure("local leaf key exceeds format bounds");
				std::copy_n(key.data(), cursor, actual.data());
				std::copy(node.prefix.begin(), node.prefix.end(), actual.begin() + cursor);
				if (paged::decode_key({actual.data(), cursor + node.prefix.size()}).kind != node.kind)
					throw ReadSourceFailure("local leaf key type mismatch");
			}
			if (node.prefix.size() > key.size() - cursor ||
				std::memcmp(node.prefix.data(), key.data() + cursor, node.prefix.size()))
				return {};
			cursor += node.prefix.size();
			if (node.kind != paged::PageKind::Branch) {
				if (cursor != key.size())
					return {};
				if (node.kind != expected)
					throw ReadSourceFailure("local leaf key type mismatch");
				return node.body;
			}
			if (cursor == key.size())
				return {};
			const auto label = static_cast<unsigned char>(key[cursor++]);
			std::size_t low = 0, high = node.labels.size();
			while (low < high) {
				const auto middle = low + (high - low) / 2;
				if (static_cast<unsigned char>(node.labels[middle]) < label)
					low = middle + 1;
				else
					high = middle;
			}
			if (low == node.labels.size() || static_cast<unsigned char>(node.labels[low]) != label)
				return {};
			const auto slot = node.slot(low);
			if (slot & 0x8000) {
				reference = paged::decode_external(bytes, slot & 0x7fff, paged_format_limits());
				resolve = true;
			} else
				at = paged::NodeAt::child(node, slot);
		}
		throw ReadSourceFailure("local page walk exceeds key depth");
	} catch (const ReadSourceFailure &) {
		throw;
	} catch (...) {
		throw ReadSourceFailure("local page walk failed");
	}
}
} // namespace detent::local::detail
