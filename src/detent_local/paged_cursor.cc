#include "paged_cursor.hh"
#include "paged_navigation.hh"
#include "read_root.hh"
namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
struct PagedCursor::Impl {
	struct Frame {
		p::Reference reference;
		p::PageHeader header;
		p::NodeView node;
		std::size_t offset = p::page_header_bytes;
		std::span<const char> bytes;
		std::size_t before = 0, length = 0, next = 0;
		bool entered = false;
	};
	d::RetirementHandle<LeasedPagedRoot> base;
	ReadView view;
	PagedReadState state;
	std::array<Frame, 203> frames{};
	std::array<char, 202> key{};
	std::size_t depth = 0, charge = 0;
	p::PageKind kind;
	bool ready = false, finished = false, failed = false, charging = false;
	Impl(d::RetirementHandle<LeasedPagedRoot> input, p::PageKind selected)
		: base(std::move(input)), kind(selected) {
		if (!base || (kind != p::PageKind::Definition && kind != p::PageKind::Namespace))
			throw std::invalid_argument("invalid paged lexical cursor");
		state = {base->root(), base->registry()->inputs()};
		rewind();
	}
	Impl(ReadView input, PagedReadState checked, p::PageKind selected)
		: view(std::move(input)), state(checked), kind(selected) {
		if (!view || (kind != p::PageKind::Definition && kind != p::PageKind::Namespace))
			throw std::invalid_argument("invalid validated paged cursor");
		rewind();
	}
	void rewind() {
		depth = state.root.pages ? 1 : 0;
		if (depth)
			frames[0] = Frame{state.root.tree};
		charge = 0;
		charging = false;
		ready = false;
		finished = !depth;
	}
	bool grant(std::size_t bytes, ValidationBudget &b) {
		if (!charging) {
			charge = bytes;
			charging = true;
		}
		auto n = std::min(charge, b.bytes);
		charge -= n;
		b.bytes -= n;
		if (charge || !b.rows)
			return false;
		--b.rows;
		charging = false;
		return true;
	}
};
PagedCursor::PagedCursor(std::shared_ptr<d::StorageLedger> account, const RecoveredGeneration &base,
						 p::PageKind kind)
	: PagedCursor(std::move(account), base.paged_, kind) {}
PagedCursor::PagedCursor(std::shared_ptr<d::StorageLedger> account, d::RetirementHandle<LeasedPagedRoot> base,
						 p::PageKind kind)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, std::move(base), kind)) {}
PagedCursor::PagedCursor(std::shared_ptr<d::StorageLedger> account, ReadView view, p::PageKind kind)
	: impl_([&] {
		  if (!view || !view.root_->paged)
			  throw std::invalid_argument("paged cursor requires a validated paged view");
		  const auto state = *view.root_->paged;
		  return d::make_storage_owner<Impl>(account, std::malloc, std::free, std::move(view), state, kind);
	  }()) {}
PagedCursor::~PagedCursor() = default;
PagedCursor::PagedCursor(PagedCursor &&) noexcept = default;
void PagedCursor::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed)
		throw std::logic_error("paged cursor unavailable");
	auto &s = *impl_;
	try {
		while (!s.ready && !s.finished) {
			auto &frame = s.frames[s.depth - 1];
			if (!frame.entered) {
				if (!frame.bytes.data()) {
					const auto segments = s.state.segments;
					const auto index = find_segment(segments, frame.reference.segment);
					if (index == segments.size())
						throw std::logic_error("missing checked cursor segment");
					frame.bytes = checked_page_bytes(segments[index], frame.reference);
					frame.header = p::decode_page_header(frame.bytes, paged_format_limits());
				}
				frame.node = p::decode_node(frame.bytes, frame.offset, paged_format_limits());
				if (!s.grant(frame.node.end - frame.offset, budget))
					return;
				if (frame.node.prefix.size() > s.key.size() - frame.before)
					throw std::logic_error("paged cursor key overflow");
				std::copy_n(frame.node.prefix.data(), frame.node.prefix.size(),
							s.key.data() + frame.before);
				frame.length = frame.before + frame.node.prefix.size();
				frame.entered = true;
				const char first = s.kind == p::PageKind::Definition ? 'K' : 'W';
				if (frame.length && s.key[0] != first) {
					--s.depth;
					if (!s.depth)
						s.finished = true;
					continue;
				}
				if (frame.node.kind != p::PageKind::Branch) {
					if (frame.node.kind != s.kind)
						throw std::logic_error("paged cursor leaf type mismatch");
					s.ready = true;
					return;
				}
			}
			if (frame.next == frame.node.labels.size()) {
				if (!s.grant(1, budget))
					return;
				--s.depth;
				if (!s.depth)
					s.finished = true;
				continue;
			}
			if (!s.grant(p::child_bytes, budget))
				return;
			const auto child = frame.next++;
			const auto slot = frame.node.slot(child);
			const auto label = static_cast<unsigned char>(frame.node.labels[child]);
			const char first = s.kind == p::PageKind::Definition ? 'K' : 'W';
			if (!frame.length && label != static_cast<unsigned char>(first))
				continue;
			if (frame.length == s.key.size() || s.depth == s.frames.size())
				throw ReadSourceFailure("paged cursor depth overflow");
			s.key[frame.length] = static_cast<char>(label);
			auto &next = s.frames[s.depth++];
			next = {};
			next.before = frame.length + 1;
			if (slot & 0x8000)
				next.reference = p::decode_external(frame.bytes, slot & 0x7fff, paged_format_limits());
			else {
				if (slot < frame.node.end || slot <= frame.offset)
					throw ReadSourceFailure("paged cursor slot does not progress");
				next.reference = frame.reference;
				next.header = frame.header;
				next.bytes = frame.bytes;
				next.offset = slot;
			}
		}
	} catch (...) {
		s.failed = true;
		throw;
	}
}
bool PagedCursor::done() const noexcept { return impl_ && !impl_->failed && impl_->finished; }
bool PagedCursor::ready() const noexcept { return impl_ && !impl_->failed && impl_->ready; }
PagedLeaf PagedCursor::leaf() const {
	if (!ready())
		throw std::logic_error("paged cursor leaf unavailable");
	const auto &s = *impl_;
	const auto &f = s.frames[s.depth - 1];
	return {f.reference,
			f.header,
			{s.key.data(), f.length},
			f.node.body};
}
void PagedCursor::consume() {
	leaf();
	auto &s = *impl_;
	s.ready = false;
	--s.depth;
	if (!s.depth)
		s.finished = true;
}
void PagedCursor::rewind() {
	if (!impl_ || impl_->failed)
		throw std::logic_error("paged cursor unavailable");
	impl_->rewind();
}
bool PagedCursor::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
