#include "paged_trie_overlay.hh"
#include "paged_navigation.hh"
#include <algorithm>
#include <vector>
namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
struct TrieOverlay::Impl {
	enum class Phase { Idle, Body, Walk, Convert, Action, Shift, Collapse, Unwind, Done };
	enum class Action { Leaf, Split, Add, Parent, Collapse };
	struct View {
		p::PageKind kind;
		std::uint64_t keys;
		std::string_view prefix;
		std::span<const char> body, old_children;
		std::span<OverlayChild> children;
		std::size_t count() const {
			return old_children.empty() ? children.size() : old_children.size() / p::child_bytes;
		}
		std::uint8_t label(std::size_t index) const {
			return old_children.empty() ? children[index].label
										: static_cast<unsigned char>(old_children[index * p::child_bytes]);
		}
		OverlayReference child(std::size_t index) const {
			return old_children.empty() ? children[index].reference
										: OverlayReference{old_children.data() + index * p::child_bytes + 8};
		}
	};
	struct Frame {
		OverlayReference parent;
		std::size_t child;
		std::uint8_t label;
	};
	std::shared_ptr<const void> owner;
	std::span<const SegmentInput> segments;
	p::Limits limits;
	d::PagedStorage arena;
	std::vector<SegmentReachability, d::StorageAllocator<SegmentReachability>> removed;
	std::array<char, p::reference_bytes> original;
	std::array<char, 202> key{};
	std::array<Frame, 203> frames{};
	std::size_t key_length = 0, offset = 0, depth = 0, charge = 0, copy = 0, common = 0, selected = 0;
	OverlayReference root, current, result;
	OverlayNode *converted = nullptr, *parent = nullptr;
	View view{};
	std::optional<std::span<const char>> incoming, previous;
	std::span<char> body;
	std::int64_t difference = 0;
	Action action = Action::Leaf;
	Phase phase = Phase::Idle;
	bool failed = false, sealed = false, changed = false, discarding = false;
	Impl(std::shared_ptr<d::StorageLedger> account, std::shared_ptr<const void> lifetime,
		 std::span<const SegmentInput> input, p::Root base, p::Limits bounds)
		: owner(std::move(lifetime)), segments(input), limits(bounds), arena(*account, 8192),
		  removed(d::StorageAllocator<SegmentReachability>(account)), original{} {
		if (!owner || segments.size() != base.segments)
			throw std::invalid_argument("missing checked overlay input");
		p::encode_root(base, limits);
		removed.resize(segments.size());
		if (base.pages) {
			original = p::encode_reference(base.tree);
			root = {original.data()};
		}
	}
	bool grant(std::size_t bytes, ValidationBudget &b) {
		if (!b.rows)
			return false;
		if (!charge)
			charge = bytes;
		auto n = std::min(charge, b.bytes);
		charge -= n;
		b.bytes -= n;
		if (charge)
			return false;
		--b.rows;
		return true;
	}
	View inspect(OverlayReference ref) const {
		if (ref.fresh) {
			const auto &n = *ref.fresh;
			return {n.kind, n.keys, n.prefix, n.body, {}, n.children};
		}
		const auto reference = p::decode_reference({ref.old, p::reference_bytes});
		const auto index = find_segment(segments, reference.segment);
		if (index == segments.size())
			throw std::invalid_argument("overlay reference outside checked registry");
		const auto bytes = checked_page_bytes(segments[index], reference);
		const auto header = p::decode_page_header(bytes, limits);
		return {header.kind,
				header.keys,
				{bytes.data() + p::page_header_bytes, header.prefix},
				bytes.subspan(p::page_header_bytes + header.prefix, header.body),
				bytes.subspan(p::page_header_bytes + header.prefix, header.children * p::child_bytes),
				{}};
	}
	OverlayNode *node(p::PageKind kind, std::uint64_t keys, std::string_view prefix,
					  std::span<const char> bytes, std::size_t capacity) {
		auto n = std::construct_at(
			static_cast<OverlayNode *>(arena.allocate(sizeof(OverlayNode), alignof(OverlayNode))));
		n->kind = kind;
		n->keys = keys;
		n->prefix = prefix;
		n->body = bytes;
		n->capacity = capacity;
		if (capacity)
			n->children = {static_cast<OverlayChild *>(
							   arena.allocate(capacity * sizeof(OverlayChild), alignof(OverlayChild))),
						   0};
		return n;
	}
	std::string_view save(std::string_view text) {
		if (text.empty())
			return {};
		auto destination = static_cast<char *>(arena.allocate(text.size(), 1));
		std::copy(text.begin(), text.end(), destination);
		return {destination, text.size()};
	}
	OverlayReference leaf(std::size_t start) {
		const auto kind = p::decode_key({key.data(), key_length}).kind;
		return {nullptr, node(kind, 1, save({key.data() + start, key_length - start}), body, 0)};
	}
	void convert(Action next) {
		action = next;
		if (current.fresh) {
			converted = current.fresh;
			phase = Phase::Action;
			return;
		}
		view = inspect(current);
		converted = nullptr;
		copy = 0;
		phase = Phase::Convert;
	}
	void complete() {
		phase = Phase::Done;
		changed = difference || previous.has_value();
	}
};
TrieOverlay::TrieOverlay(std::shared_ptr<d::StorageLedger> account, std::shared_ptr<const void> owner,
						 std::span<const SegmentInput> inputs, p::Root base, p::Limits limits)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, std::move(owner), inputs,
										base, limits)) {}
TrieOverlay::~TrieOverlay() = default;
TrieOverlay::TrieOverlay(TrieOverlay &&) noexcept = default;
void TrieOverlay::change(std::string_view key, std::optional<std::span<const char>> body) {
	if (!impl_ || impl_->failed || impl_->sealed ||
		(impl_->phase != Impl::Phase::Idle && impl_->phase != Impl::Phase::Done))
		throw std::logic_error("overlay operation unavailable");
	const auto parsed = p::decode_key(key);
	if (body && (body->size() > p::page_bytes - p::page_header_bytes - key.size() ||
				 (parsed.kind == p::PageKind::Namespace ? body->size() != 8 : body->size() < 24)))
		throw std::invalid_argument("overlay leaf framing");
	auto &s = *impl_;
	std::copy(key.begin(), key.end(), s.key.begin());
	s.key_length = key.size();
	s.incoming = body;
	s.previous = {};
	s.body = {};
	s.copy = s.offset = s.depth = s.charge = 0;
	s.current = s.root;
	s.difference = 0;
	s.changed = false;
	s.phase = Impl::Phase::Body;
}
void TrieOverlay::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed || impl_->sealed)
		throw std::logic_error("overlay unavailable");
	auto &s = *impl_;
	using Phase = Impl::Phase;
	using Action = Impl::Action;
	try {
		while (s.phase != Phase::Idle && s.phase != Phase::Done) {
			switch (s.phase) {
			case Phase::Body:
				if (s.incoming) {
					if (!s.body.data()) {
						if (!s.grant(sizeof(OverlayNode), budget))
							return;
						s.body = {static_cast<char *>(s.arena.allocate(s.incoming->size(), 1)),
								  s.incoming->size()};
					}
					if (s.copy != s.body.size()) {
						if (!budget.rows || !budget.bytes)
							return;
						auto n = std::min({s.body.size() - s.copy, budget.bytes, std::size_t(8192)});
						std::copy_n(s.incoming->data() + s.copy, n, s.body.data() + s.copy);
						s.copy += n;
						budget.bytes -= n;
						--budget.rows;
						continue;
					}
				}
				s.phase = Phase::Walk;
				break;
			case Phase::Walk: {
				if (!s.current) {
					if (!s.incoming) {
						s.complete();
						break;
					}
					if (!s.grant(sizeof(OverlayNode) + s.key_length, budget))
						return;
					s.result = s.leaf(s.offset);
					s.difference = 1;
					s.phase = Phase::Unwind;
					break;
				}
				s.view = s.inspect(s.current);
				if (!s.grant(p::page_header_bytes + s.view.prefix.size() + p::child_bytes + 8, budget))
					return;
				s.common = 0;
				while (s.common < s.view.prefix.size() && s.offset + s.common < s.key_length &&
					   s.view.prefix[s.common] == s.key[s.offset + s.common])
					++s.common;
				if (s.common != s.view.prefix.size()) {
					if (!s.incoming) {
						s.complete();
						break;
					}
					s.convert(Action::Split);
					break;
				}
				if (s.view.kind != p::PageKind::Branch) {
					if (s.offset + s.common != s.key_length)
						throw std::logic_error("non-prefix-free overlay key");
					s.previous = s.view.body;
					s.difference = s.incoming ? 0 : -1;
					s.convert(Action::Leaf);
					break;
				}
				s.offset += s.common;
				if (s.offset == s.key_length)
					throw std::logic_error("overlay branch terminal key");
				const auto label = static_cast<unsigned char>(s.key[s.offset++]);
				std::size_t low = 0, high = s.view.count();
				while (low < high) {
					auto mid = low + (high - low) / 2;
					if (s.view.label(mid) < label)
						low = mid + 1;
					else
						high = mid;
				}
				s.selected = low;
				if (low == s.view.count() || s.view.label(low) != label) {
					if (!s.incoming) {
						s.complete();
						break;
					}
					s.difference = 1;
					s.convert(Action::Add);
					break;
				}
				if (s.depth == s.frames.size())
					throw std::length_error("overlay path depth");
				s.frames[s.depth++] = {s.current, low, label};
				s.current = s.view.child(low);
				break;
			}
			case Phase::Convert:
				if (!s.converted) {
					if (!s.grant(sizeof(OverlayNode), budget))
						return;
					const auto capacity = s.view.count() + (s.action == Action::Add ? 1 : 0);
					s.converted = s.node(s.view.kind, s.view.keys, s.view.prefix, s.view.body, capacity);
				}
				if (s.copy < s.view.count()) {
					if (!s.grant(p::child_bytes + sizeof(OverlayChild), budget))
						return;
					auto data = s.converted->children.data();
					std::construct_at(data + s.copy,
									  OverlayChild{s.view.label(s.copy), s.view.child(s.copy)});
					++s.copy;
					s.converted->children = {data, s.copy};
					break;
				}
				{
					const auto ref = p::decode_reference({s.current.old, p::reference_bytes});
					const auto index = find_segment(s.segments, ref.segment);
					++s.removed[index].pages;
					s.removed[index].bytes += ref.length;
					s.phase = Phase::Action;
				}
				break;
			case Phase::Action: {
				if (!s.grant(3 * sizeof(OverlayNode) + 2 * s.key_length, budget))
					return;
				auto &n = *s.converted;
				if (s.action == Action::Leaf) {
					if (s.incoming) {
						n.body = s.body;
						s.result = {nullptr, &n};
					} else
						s.result = {};
					s.phase = Phase::Unwind;
				} else if (s.action == Action::Split) {
					if (s.offset + s.common == s.key_length)
						throw std::logic_error("overlay prefix-free split");
					const auto old_label = static_cast<unsigned char>(n.prefix[s.common]);
					const auto new_label = static_cast<unsigned char>(s.key[s.offset + s.common]);
					const auto prefix = n.prefix.substr(0, s.common);
					n.prefix.remove_prefix(s.common + 1);
					auto branch = s.node(p::PageKind::Branch, n.keys + 1, prefix, {}, 2);
					auto leaf = s.leaf(s.offset + s.common + 1);
					const OverlayChild old{old_label, {nullptr, &n}}, fresh{new_label, leaf};
					std::construct_at(branch->children.data(), old_label < new_label ? old : fresh);
					std::construct_at(branch->children.data() + 1, old_label < new_label ? fresh : old);
					branch->children = {branch->children.data(), 2};
					s.result = {nullptr, branch};
					s.difference = 1;
					s.phase = Phase::Unwind;
				} else if (s.action == Action::Add) {
					if (n.children.size() == 256)
						throw std::length_error("overlay branch arity");
					if (n.capacity == n.children.size()) {
						const auto capacity =
							std::min<std::size_t>(256, std::max(n.capacity + 1, n.capacity * 2));
						auto rows = static_cast<OverlayChild *>(
							s.arena.allocate(capacity * sizeof(OverlayChild), alignof(OverlayChild)));
						// At most 256 trivial rows; the next shift phase charges each copy.
						s.view = {n.kind, n.keys, n.prefix, n.body, {}, n.children};
						n.children = {rows, 0};
						n.capacity = capacity;
						s.copy = 0;
						s.phase = Phase::Shift;
						s.parent = &n;
						s.result = s.leaf(s.offset);
						break;
					}
					s.parent = &n;
					s.result = s.leaf(s.offset);
					s.copy = n.children.size();
					s.phase = Phase::Shift;
					s.view = {};
				} else if (s.action == Action::Parent) {
					s.parent = &n;
					if (s.result) {
						n.children[s.selected].reference = s.result;
						n.keys += s.difference;
						s.result = {nullptr, &n};
						s.phase = Phase::Collapse;
					} else {
						s.copy = s.selected;
						s.view = {};
						s.phase = Phase::Shift;
					}
				} else {
					const auto prefix = s.parent->prefix;
					const auto length = prefix.size() + 1 + n.prefix.size();
					if (length > s.key.size())
						throw std::length_error("overlay collapsed prefix");
					std::array<char, 202> joined{};
					std::copy(prefix.begin(), prefix.end(), joined.begin());
					joined[prefix.size()] = static_cast<char>(s.parent->children[0].label);
					std::copy(n.prefix.begin(), n.prefix.end(), joined.begin() + prefix.size() + 1);
					n.prefix = s.save({joined.data(), length});
					s.result = {nullptr, &n};
					s.phase = Phase::Unwind;
				}
				break;
			}
			case Phase::Shift: {
				auto &n = *s.parent;
				if (s.action == Action::Add && !s.view.children.empty()) {
					if (s.copy < s.view.children.size()) {
						if (!s.grant(sizeof(OverlayChild), budget))
							return;
						std::construct_at(n.children.data() + s.copy, s.view.children[s.copy]);
						++s.copy;
						n.children = {n.children.data(), s.copy};
						break;
					}
					s.view = {};
					s.copy = n.children.size();
				}
				if (s.action == Action::Add) {
					if (s.copy > s.selected) {
						if (!s.grant(sizeof(OverlayChild), budget))
							return;
						std::construct_at(n.children.data() + s.copy, n.children[s.copy - 1]);
						--s.copy;
						break;
					}
					if (!s.grant(sizeof(OverlayChild), budget))
						return;
					std::construct_at(n.children.data() + s.selected,
									  OverlayChild{static_cast<std::uint8_t>(s.key[s.offset - 1]), s.result});
					n.children = {n.children.data(), n.children.size() + 1};
					++n.keys;
				} else {
					if (s.copy + 1 < n.children.size()) {
						if (!s.grant(sizeof(OverlayChild), budget))
							return;
						n.children[s.copy] = n.children[s.copy + 1];
						++s.copy;
						break;
					}
					if (!s.grant(sizeof(OverlayChild), budget))
						return;
					n.children = n.children.first(n.children.size() - 1);
					n.keys += s.difference;
				}
				s.result = {nullptr, &n};
				s.phase = Phase::Collapse;
				break;
			}
			case Phase::Collapse:
				if (s.result.fresh->children.size() == 1) {
					s.parent = s.result.fresh;
					s.current = s.parent->children[0].reference;
					s.convert(Action::Collapse);
				} else
					s.phase = Phase::Unwind;
				break;
			case Phase::Unwind:
				if (!s.depth) {
					s.root = s.result;
					s.complete();
					break;
				}
				{
					const auto frame = s.frames[--s.depth];
					s.current = frame.parent;
					s.selected = frame.child;
					s.convert(Action::Parent);
				}
				break;
			default:
				break;
			}
		}
	} catch (...) {
		s.failed = true;
		throw;
	}
}
bool TrieOverlay::operation_done() const noexcept {
	return impl_ && !impl_->failed && impl_->phase == Impl::Phase::Done;
}
bool TrieOverlay::changed() const {
	if (!operation_done())
		throw std::logic_error("overlay operation incomplete");
	return impl_->changed;
}
std::optional<std::span<const char>> TrieOverlay::previous_body() const {
	changed();
	return impl_->previous;
}
void TrieOverlay::seal() {
	if (!impl_ || impl_->failed || (impl_->phase != Impl::Phase::Idle && !operation_done()))
		throw std::logic_error("overlay incomplete");
	impl_->sealed = true;
}
OverlayReference TrieOverlay::root() const {
	if (!impl_ || !impl_->sealed || impl_->failed)
		throw std::logic_error("unsealed overlay");
	return impl_->root;
}
std::span<const SegmentReachability> TrieOverlay::removed() const {
	root();
	return impl_->removed;
}
bool TrieOverlay::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	auto &s = *impl_;
	s.failed = true;
	if (!s.discarding) {
		s.discarding = true;
		s.charge = 0;
	}
	while (s.arena.front_capacity()) {
		if (!budget.rows)
			return false;
		if (!s.charge)
			s.charge = s.arena.front_capacity();
		auto n = std::min(s.charge, budget.bytes);
		s.charge -= n;
		budget.bytes -= n;
		if (s.charge)
			return false;
		--budget.rows;
		s.arena.release_one();
	}
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
