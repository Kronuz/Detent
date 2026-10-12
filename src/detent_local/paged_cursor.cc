#include "paged_cursor.hh"
#include "paged_navigation.hh"
namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
struct PagedCursor::Impl {
	struct Frame {
		p::Reference reference;
		p::PageHeader header;
		std::span<const char> bytes;
		std::size_t before = 0, length = 0, next = 0;
		bool entered = false;
	};
	d::RetirementHandle<LeasedPagedRoot> base;
	std::array<Frame, 203> frames{};
	std::array<char, 202> key{};
	std::size_t depth = 0, charge = 0;
	p::PageKind kind;
	bool ready = false, finished = false, failed = false, charging = false;
	Impl(d::RetirementHandle<LeasedPagedRoot> input, p::PageKind selected)
		: base(std::move(input)), kind(selected) {
		if (!base || (kind != p::PageKind::Definition && kind != p::PageKind::Namespace))
			throw std::invalid_argument("invalid paged lexical cursor");
		rewind();
	}
	void rewind() {
		depth = base->root().pages ? 1 : 0;
		if (depth)
			frames[0] = Frame{base->root().tree};
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
					const auto segments = s.base->registry()->inputs();
					const auto index = find_segment(segments, frame.reference.segment);
					if (index == segments.size())
						throw std::logic_error("missing checked cursor segment");
					frame.bytes = checked_page_bytes(segments[index], frame.reference);
					frame.header = p::decode_page_header(frame.bytes);
				}
				if (!s.grant(p::page_header_bytes + frame.header.prefix, budget))
					return;
				if (frame.header.prefix > s.key.size() - frame.before)
					throw std::logic_error("paged cursor key overflow");
				std::copy_n(frame.bytes.data() + p::page_header_bytes, frame.header.prefix,
							s.key.data() + frame.before);
				frame.length = frame.before + frame.header.prefix;
				frame.entered = true;
				const char first = s.kind == p::PageKind::Definition ? 'K' : 'W';
				if (frame.length && s.key[0] != first) {
					--s.depth;
					if (!s.depth)
						s.finished = true;
					continue;
				}
				if (frame.header.kind != p::PageKind::Branch) {
					if (frame.header.kind != s.kind)
						throw std::logic_error("paged cursor leaf type mismatch");
					s.ready = true;
					return;
				}
			}
			if (frame.next == frame.header.children) {
				if (!s.grant(1, budget))
					return;
				--s.depth;
				if (!s.depth)
					s.finished = true;
				continue;
			}
			if (!s.grant(p::child_bytes, budget))
				return;
			const auto offset = p::page_header_bytes + frame.header.prefix + frame.next++ * p::child_bytes;
			const auto child = p::decode_child(frame.bytes.subspan(offset, p::child_bytes));
			const char first = s.kind == p::PageKind::Definition ? 'K' : 'W';
			if (!frame.length && child.label != static_cast<unsigned char>(first))
				continue;
			if (frame.length == s.key.size() || s.depth == s.frames.size())
				throw std::logic_error("paged cursor depth overflow");
			s.key[frame.length] = static_cast<char>(child.label);
			s.frames[s.depth++] = Impl::Frame{child.reference, {}, {}, frame.length + 1};
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
			f.bytes.subspan(p::page_header_bytes + f.header.prefix, f.header.body)};
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
