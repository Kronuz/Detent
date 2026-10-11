#include "paged_overlay_assembler.hh"
#include "paged_image_state.hh"
#include "paged_page_builder.hh"
#include "paged_segment_builder.hh"
namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
namespace {
void require(bool condition) {
	if (!condition)
		throw std::invalid_argument("invalid sparse paged assembly");
}
} // namespace
struct OverlayAssembler::Impl {
	struct Frame {
		const OverlayNode *node = nullptr;
		std::span<p::Child> children;
		std::size_t next = 0;
		bool entered = false;
	};
	enum class Phase {
		Account,
		Count,
		Allocate,
		Visit,
		Page,
		Pack,
		ReleasePage,
		Commit,
		SealPack,
		RegisterPack,
		ReleasePack,
		Finish,
		Root,
		Done
	};
	std::shared_ptr<d::StorageLedger> account;
	const TrieOverlay &overlay;
	std::span<const SegmentInput> inputs;
	std::span<const SegmentReachability> live, removed;
	p::Root root;
	StorageIdentity storage;
	GenerationId generation;
	p::Limits limits;
	d::PagedStorage arena;
	std::array<Frame, 203> frames{};
	std::size_t depth = 0, position = 0, charge = 0;
	std::uint64_t fresh_pages = 0, fresh_bytes = 0, reused_segments = 0, physical_bytes = 0, base_pages = 0,
				  base_bytes = 0, remaining_pages = 0, remaining_bytes = 0;
	std::optional<PagedImage> output;
	std::optional<PageBuilder> page;
	std::optional<SegmentBuilder> segment;
	p::Reference reference;
	Phase phase = Phase::Account, after_pack = Phase::Visit;
	bool failed = false, discarding = false;
	Impl(std::shared_ptr<d::StorageLedger> ledger, const TrieOverlay &source,
		 std::span<const SegmentInput> segments, std::span<const SegmentReachability> reachability,
		 p::Root base, StorageIdentity identity, GenerationId publication, d::Cursor target, p::Limits bounds)
		: account(std::move(ledger)), overlay(source), inputs(segments), live(reachability),
		  removed(overlay.removed()), root(base), storage(identity), generation(publication), limits(bounds),
		  arena(*account, 8192) {
		require(inputs.size() == live.size() && live.size() == removed.size() &&
				live.size() == root.segments);
		require(root.cursor.same_identity(target) && target.revision >= root.cursor.revision);
		require(std::any_of(storage.begin(), storage.end(), [](char c) { return c; }) &&
				std::any_of(generation.begin(), generation.end(), [](char c) { return c; }));
		p::encode_root(root, limits);
		root.cursor = target;
		if (const auto tree = overlay.root(); tree.fresh) {
			require(tree.fresh->keys >= root.namespaces &&
					tree.fresh->keys - root.namespaces <= limits.records);
			root.records = tree.fresh->keys - root.namespaces;
			frames[0].node = tree.fresh;
			depth = 1;
		} else if (!tree) {
			require(!root.namespaces);
			root.records = 0;
		}
	}
	bool grant(std::size_t bytes, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = bytes;
		auto n = std::min(charge, budget.bytes);
		charge -= n;
		budget.bytes -= n;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
	void push(const OverlayNode *node) {
		require(depth < frames.size());
		frames[depth++] = Frame{node};
	}
};
OverlayAssembler::OverlayAssembler(std::shared_ptr<d::StorageLedger> account, const TrieOverlay &overlay,
								   std::span<const SegmentInput> segments,
								   std::span<const SegmentReachability> live, p::Root base,
								   StorageIdentity storage, GenerationId generation, d::Cursor target,
								   p::Limits limits)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, overlay, segments, live,
										base, storage, generation, target, limits)) {}
OverlayAssembler::~OverlayAssembler() = default;
OverlayAssembler::OverlayAssembler(OverlayAssembler &&) noexcept = default;
void OverlayAssembler::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed || impl_->discarding)
		throw std::logic_error("inactive sparse paged assembly");
	auto &s = *impl_;
	try {
		while (s.phase != Impl::Phase::Done) {
			switch (s.phase) {
			case Impl::Phase::Account:
				if (s.position == s.inputs.size()) {
					require(s.base_pages == s.root.pages && s.base_bytes == s.root.bytes);
					s.root.pages = s.remaining_pages;
					s.root.bytes = s.remaining_bytes;
					s.phase = Impl::Phase::Count;
					break;
				}
				if (!s.grant(sizeof(SegmentReachability) * 2 + sizeof(SegmentOwnership), budget))
					return;
				{
					const auto i = s.position++;
					require(s.removed[i].pages <= s.live[i].pages && s.removed[i].bytes <= s.live[i].bytes);
					require(s.live[i].pages <= s.root.pages - s.base_pages &&
							s.live[i].bytes <= s.root.bytes - s.base_bytes);
					s.base_pages += s.live[i].pages;
					s.base_bytes += s.live[i].bytes;
					s.remaining_pages += s.live[i].pages - s.removed[i].pages;
					s.remaining_bytes += s.live[i].bytes - s.removed[i].bytes;
					if (s.live[i].pages != s.removed[i].pages) {
						++s.reused_segments;
						require(s.inputs[i].ownership.length <= s.limits.bytes - s.physical_bytes);
						s.physical_bytes += s.inputs[i].ownership.length;
					}
				}
				break;
			case Impl::Phase::Count:
				if (!s.depth) {
					s.phase = Impl::Phase::Allocate;
					break;
				}
				{
					auto &frame = s.frames[s.depth - 1];
					const auto &node = *frame.node;
					if (!frame.entered) {
						if (!s.grant(p::page_header_bytes + node.prefix.size(), budget))
							return;
						require(node.children.size() <= 256 && node.prefix.size() <= 202 &&
								node.body.size() <= p::page_bytes);
						const auto length = p::page_header_bytes + node.prefix.size() +
											node.children.size() * p::child_bytes + node.body.size();
						require(length <= p::page_bytes && s.fresh_pages < s.limits.pages &&
								length <= s.limits.bytes - s.fresh_bytes);
						++s.fresh_pages;
						s.fresh_bytes += length;
						frame.entered = true;
					}
					if (frame.next == node.children.size()) {
						--s.depth;
						break;
					}
					if (!s.grant(sizeof(OverlayChild), budget))
						return;
					const auto child = node.children[frame.next++].reference;
					require(bool(child));
					if (child.fresh)
						s.push(child.fresh);
				}
				break;
			case Impl::Phase::Allocate: {
				if (!s.grant(sizeof(PagedImage), budget))
					return;
				const auto capacity = std::min(s.fresh_pages, (s.fresh_bytes + 7 * s.fresh_pages +
															   p::segment_bytes - p::page_bytes - 1) /
																  (p::segment_bytes - p::page_bytes));
				require(capacity <= s.limits.segments && s.fresh_pages <= s.limits.pages - s.root.pages &&
						s.fresh_bytes <= s.limits.bytes - s.root.bytes);
				s.output.emplace(PagedImage(s.account, capacity));
				if (const auto tree = s.overlay.root(); tree.fresh) {
					s.frames[0] = Impl::Frame{tree.fresh};
					s.depth = 1;
					s.phase = Impl::Phase::Visit;
				} else {
					s.root.tree =
						tree.old ? p::decode_reference({tree.old, p::reference_bytes}) : p::Reference{};
					s.phase = Impl::Phase::Finish;
				}
				break;
			}
			case Impl::Phase::Visit: {
				auto &frame = s.frames[s.depth - 1];
				const auto &node = *frame.node;
				if (!frame.entered) {
					if (!s.grant(p::page_header_bytes + node.prefix.size() +
									 sizeof(p::Child) * node.children.size(),
								 budget))
						return;
					if (!node.children.empty())
						frame.children = {static_cast<p::Child *>(s.arena.allocate(
											  sizeof(p::Child) * node.children.size(), alignof(p::Child))),
										  node.children.size()};
					frame.entered = true;
				}
				if (frame.next < node.children.size()) {
					if (!s.grant(p::reference_bytes, budget))
						return;
					const auto child = node.children[frame.next];
					if (child.reference.fresh)
						s.push(child.reference.fresh);
					else {
						std::construct_at(&frame.children[frame.next++],
										  p::Child{child.label, p::decode_reference({child.reference.old,
																					 p::reference_bytes})});
					}
					break;
				}
				s.page.emplace(s.account, node.kind, node.keys, node.prefix, frame.children, node.body,
							   s.limits);
				s.phase = Impl::Phase::Page;
				break;
			}
			case Impl::Phase::Page:
				s.page->step(budget);
				if (!s.page->done())
					return;
				if (!s.segment)
					s.segment.emplace(s.account, s.storage, s.generation, s.output->impl_->segments.size());
				if (!s.segment->fits(*s.page)) {
					s.after_pack = Impl::Phase::Page;
					s.segment->seal();
					s.phase = Impl::Phase::SealPack;
				} else {
					s.segment->append(*s.page);
					s.phase = Impl::Phase::Pack;
				}
				break;
			case Impl::Phase::Pack:
				s.segment->step(budget);
				if (!s.segment->page_ready())
					return;
				s.reference = s.segment->reference();
				++s.root.pages;
				s.root.bytes += s.reference.length;
				s.phase = Impl::Phase::ReleasePage;
				break;
			case Impl::Phase::ReleasePage:
				if (!s.page->discard(budget))
					return;
				s.page.reset();
				s.phase = Impl::Phase::Commit;
				break;
			case Impl::Phase::Commit:
				if (!s.grant(p::reference_bytes, budget))
					return;
				--s.depth;
				if (s.depth) {
					auto &parent = s.frames[s.depth - 1];
					std::construct_at(&parent.children[parent.next],
									  p::Child{parent.node->children[parent.next].label, s.reference});
					++parent.next;
					s.phase = Impl::Phase::Visit;
				} else {
					s.root.tree = s.reference;
					s.segment->seal();
					s.after_pack = Impl::Phase::Finish;
					s.phase = Impl::Phase::SealPack;
				}
				break;
			case Impl::Phase::SealPack:
				s.segment->step(budget);
				if (!s.segment->done())
					return;
				s.phase = Impl::Phase::RegisterPack;
				break;
			case Impl::Phase::RegisterPack:
				if (!s.grant(sizeof(BuiltSegment), budget))
					return;
				{
					const auto ownership = s.segment->ownership();
					require(s.output->impl_->segments.size() + s.reused_segments < s.limits.segments &&
							ownership.length <= s.limits.bytes - s.physical_bytes);
					s.physical_bytes += ownership.length;
					s.output->impl_->segments.push_back({ownership, s.segment->bytes()});
				}
				s.phase = Impl::Phase::ReleasePack;
				break;
			case Impl::Phase::ReleasePack:
				if (!s.segment->discard(budget))
					return;
				s.segment.reset();
				s.phase = s.after_pack;
				break;
			case Impl::Phase::Finish:
				s.root.segments = s.reused_segments + s.output->impl_->segments.size();
				s.root.logical = s.root.pages ? s.root.tree.logical : p::empty_commitment();
				s.phase = Impl::Phase::Root;
				break;
			case Impl::Phase::Root:
				if (!s.grant(3 * p::root_bytes, budget))
					return;
				{
					const auto encoded = p::encode_root(s.root, s.limits);
					auto bytes = d::make_owned_buffer(s.account, encoded.size());
					bytes->append({encoded.data(), encoded.size()});
					bytes->seal();
					auto &output = *s.output->impl_;
					output.root = s.root;
					output.bytes = std::move(bytes);
					output.descriptor = {s.generation, p::root_bytes, p::physical_commitment(encoded),
										 s.root.cursor, s.root.profile};
					output.sealed = true;
				}
				s.phase = Impl::Phase::Done;
				break;
			case Impl::Phase::Done:
				break;
			}
		}
	} catch (...) {
		s.failed = true;
		throw;
	}
}
bool OverlayAssembler::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->phase == Impl::Phase::Done;
}
PagedImage OverlayAssembler::take() {
	if (!done())
		throw std::logic_error("sparse paged assembly incomplete");
	auto output = std::move(*impl_->output);
	impl_->output.reset();
	impl_->discarding = true;
	return output;
}
bool OverlayAssembler::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	auto &s = *impl_;
	if (!s.discarding) {
		s.discarding = true;
		s.charge = 0;
	}
	if (s.page && !s.page->discard(budget))
		return false;
	s.page.reset();
	if (s.segment && !s.segment->discard(budget))
		return false;
	s.segment.reset();
	if (s.output && !s.output->discard(budget))
		return false;
	s.output.reset();
	while (s.arena.front_capacity()) {
		if (!s.grant(s.arena.front_capacity(), budget))
			return false;
		s.arena.release_one();
	}
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
