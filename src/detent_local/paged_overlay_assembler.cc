#include "paged_overlay_assembler.hh"
#include "paged_image_state.hh"
#include "paged_navigation.hh"
#include "paged_packer.hh"
#include "paged_segment_builder.hh"
#include <map>
#include <vector>
namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
namespace {
void require(bool condition) {
	if (!condition)
		throw std::invalid_argument("invalid sparse paged assembly");
}
template <class T> using Vector = std::vector<T, d::StorageAllocator<T>>;
} // namespace
struct OverlayAssembler::Impl {
	struct Frame {
		OverlayReference reference;
		std::size_t length = 0, next = 0;
		bool entered = false;
	};
	struct Extent {
		p::Reference reference;
		bool retained = false;
	};
	enum class Phase {
		Allocate,
		Visit,
		Packer,
		Page,
		Pack,
		Commit,
		SealPack,
		RegisterPack,
		ReleasePack,
		Account,
		Root,
		Done
	};
	std::shared_ptr<d::StorageLedger> account;
	const TrieOverlay &overlay;
	std::span<const SegmentInput> inputs;
	std::span<const SegmentReachability> live;
	Vector<SegmentReachability> removed;
	Vector<Extent> extents;
	using ExtentKey = std::pair<StorageIdentity, std::uint32_t>;
	using ExtentRow = std::pair<const ExtentKey, std::size_t>;
	std::map<ExtentKey, std::size_t, std::less<ExtentKey>, d::StorageAllocator<ExtentRow>>
		extent_index;
	p::Root root, base;
	StorageIdentity storage;
	GenerationId generation;
	p::Limits limits;
	std::uint64_t maximum_output_bytes, output_bytes = 0, fresh_pages = 0, fresh_bytes = 0;
	std::uint64_t base_pages = 0, base_bytes = 0, reused_segments = 0, physical_bytes = 0;
	std::array<Frame, 203> frames{};
	std::array<char, 202> path{};
	std::array<std::uint8_t, 256> labels{};
	std::size_t depth = 0, position = 0, charge = 0, touched = 0;
	std::optional<PagedImage> output;
	std::optional<PagedPacker> packer;
	std::optional<SegmentBuilder> segment;
	SealedPage page;
	p::Reference reference;
	std::shared_ptr<d::OwnedBuffer> relocated;
	Phase phase = Phase::Allocate, after_pack = Phase::Visit;
	bool failed = false, discarding = false, finishing = false, event = false, relocation = false;
	Impl(std::shared_ptr<d::StorageLedger> ledger, const TrieOverlay &source,
		 std::span<const SegmentInput> segments, std::span<const SegmentReachability> reachability,
		 p::Root previous, StorageIdentity identity, GenerationId publication, d::Cursor target,
		 p::Limits bounds, std::uint64_t maximum)
		: account(std::move(ledger)), overlay(source), inputs(segments), live(reachability),
		  removed(d::StorageAllocator<SegmentReachability>(account)),
		  extents(d::StorageAllocator<Extent>(account)),
		  extent_index(std::less<ExtentKey>{}, d::StorageAllocator<ExtentRow>(account)),
		  root(previous), base(previous), storage(identity), generation(publication),
		  limits(bounds), maximum_output_bytes(maximum) {
		require(inputs.size() == live.size() && live.size() == root.segments);
		require(root.cursor.same_identity(target) && target.revision >= root.cursor.revision);
		p::encode_root(root, limits);
		root.cursor = target;
		root.namespaces = overlay.namespaces();
		removed.resize(inputs.size());
		const auto tree = overlay.root();
		if (tree) {
			frames[0].reference = tree;
			depth = 1;
			if (tree.fresh) {
				relocation = !tree.fresh->relocation_bytes.empty();
				require(tree.fresh->keys >= root.namespaces &&
						tree.fresh->keys - root.namespaces <= limits.records);
				root.records = tree.fresh->keys - root.namespaces;
			}
		} else {
			require(!root.namespaces);
			root.records = 0;
		}
		if (!relocation)
			packer.emplace(account, limits);
	}
	bool grant(std::size_t amount, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = amount;
		const auto n = std::min(charge, budget.bytes);
		charge -= n;
		budget.bytes -= n;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
	std::span<const char> bytes(const p::Reference &r) const {
		const auto i = find_segment(inputs, r.segment);
		require(i != inputs.size());
		return checked_page_bytes(inputs[i], r);
	}
	void touch(const p::Reference &r, bool retain = false) {
		auto [found, inserted] =
			extent_index.emplace(ExtentKey{r.segment, r.offset}, extents.size());
		if (inserted)
			extents.push_back({r, retain});
		else if (retain) {
			auto &extent = extents[found->second];
			require(!extent.retained);
			extent.retained = true;
		}
	}
	std::optional<p::Reference> corresponding(std::string_view path) const {
		if (!base.pages)
			return {};
		auto r = base.tree;
		p::NodeAt node;
		std::size_t at = 0;
		for (;;) {
			if (at == path.size())
				return node.first() ? std::optional(r) : std::nullopt;
			const auto b = bytes(r);
			const auto n = node.decode(b, limits);
			if (n.kind != p::PageKind::Branch || path.size() - at <= n.prefix.size() ||
				path.substr(at, n.prefix.size()) != n.prefix)
				return {};
			at += n.prefix.size();
			const auto label = static_cast<unsigned char>(path[at++]);
			auto found = std::lower_bound(
				n.labels.begin(), n.labels.end(), label,
				[](char a, unsigned char b) { return static_cast<unsigned char>(a) < b; });
			if (found == n.labels.end() || static_cast<unsigned char>(*found) != label)
				return {};
			const auto slot = n.slot(found - n.labels.begin());
			if (slot & 0x8000) {
				r = p::decode_external(b, slot & 0x7fff, limits);
				node = {};
			} else
				node = p::NodeAt::child(n, slot);
		}
	}
	void push(OverlayReference ref, std::string_view path) {
		require(depth < frames.size() && path.size() <= 202);
		auto &f = frames[depth++];
		f = {};
		f.reference = ref;
		f.length = path.size();
		std::copy(path.begin(), path.end(), this->path.begin());
	}
	void finish_page() {
		++fresh_pages;
		fresh_bytes += reference.length;
		require(fresh_pages <= limits.pages && fresh_bytes <= limits.bytes);
	}
};
OverlayAssembler::OverlayAssembler(std::shared_ptr<d::StorageLedger> account,
								   const TrieOverlay &overlay,
								   std::span<const SegmentInput> segments,
								   std::span<const SegmentReachability> live, p::Root base,
								   StorageIdentity storage, GenerationId generation,
								   d::Cursor target, p::Limits limits, std::uint64_t maximum)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, overlay, segments,
										live, base, storage, generation, target, limits, maximum)) {
}
OverlayAssembler::~OverlayAssembler() = default;
OverlayAssembler::OverlayAssembler(OverlayAssembler &&) noexcept = default;
void OverlayAssembler::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed || impl_->discarding)
		throw std::logic_error("inactive sparse paged assembly");
	auto &s = *impl_;
	using Phase = Impl::Phase;
	try {
		while (s.phase != Phase::Done) {
			switch (s.phase) {
			case Phase::Allocate:
				if (s.touched < s.overlay.touched_pages().size()) {
					if (!s.grant(sizeof(p::Reference), budget))
						return;
					s.touch(s.overlay.touched_pages()[s.touched++]);
					break;
				}
				if (!s.grant(sizeof(PagedImage), budget))
					return;
				s.output.emplace(PagedImage(s.account, 0));
				s.phase = Phase::Visit;
				break;
			case Phase::Visit: {
				if (s.relocation) {
					auto &f = s.frames[s.depth - 1];
					const auto &n = *f.reference.fresh;
					if (f.next < n.children.size()) {
						if (!s.grant(sizeof(OverlayReference), budget))
							return;
						const auto child = n.children[f.next++].reference;
						if (child.fresh)
							s.push(child, {});
						break;
					}
					if (!f.entered) {
						if (!s.grant(n.relocation_bytes.size(), budget))
							return;
						s.relocated = d::make_owned_buffer(s.account, n.relocation_bytes.size());
						s.relocated->append({n.relocation_bytes.data(), n.relocation_bytes.size()});
						auto bytes = s.relocated->mutable_bytes();
						const auto count = n.children.size();
						for (std::size_t i = 0; i < count; ++i) {
							const auto &child = n.children[i].reference;
							const auto placement = p::encode_placement(
								child.old ? *child.old : child.fresh->relocation_reference);
							std::copy(placement.begin(), placement.end(),
									  bytes.end() - p::placement_bytes * (count - i));
						}
						s.relocated->seal();
						s.page = {s.relocated, n.relocation_reference, {}};
						f.entered = true;
					}
					s.phase = Phase::Page;
					break;
				}
				if (s.event) {
					if (!s.packer->accepted()) {
						s.phase = Phase::Packer;
						break;
					}
					s.event = false;
					--s.depth;
				}
				if (!s.depth) {
					if (!s.finishing) {
						s.packer->finish();
						s.finishing = true;
					}
					s.phase = Phase::Packer;
					break;
				}
				auto &f = s.frames[s.depth - 1];
				if (!s.grant(sizeof(TrieEvent) + sizeof(OverlayReference), budget))
					return;
				if (f.reference.old) {
					const auto &r = *f.reference.old;
					if (f.reference.at.first() && r.length > p::page_capacity)
						s.packer->push_reference(r, std::string_view(s.path.data(), f.length));
					else {
						s.touch(r);
						s.packer->push_cluster(s.bytes(r), f.reference.at,
											   std::string_view(s.path.data(), f.length));
					}
					s.event = true;
					s.phase = Phase::Packer;
					break;
				}
				const auto &n = *f.reference.fresh;
				if (f.next < n.children.size()) {
					const auto child = n.children[f.next++];
					auto path = s.path;
					const auto length = f.length + n.prefix.size() + 1;
					require(length <= path.size());
					std::copy(n.prefix.begin(), n.prefix.end(), path.begin() + f.length);
					path[length - 1] = static_cast<char>(child.label);
					s.push(child.reference, {path.data(), length});
					break;
				}
				std::copy(n.prefix.begin(), n.prefix.end(), s.path.begin() + f.length);
				for (std::size_t i = 0; i < n.children.size(); ++i)
					s.labels[i] = n.children[i].label;
				s.packer->push({n.kind,
								0,
								n.keys,
								{s.path.data(), f.length + n.prefix.size()},
								n.prefix,
								{s.labels.data(), n.children.size()}},
							   n.body);
				s.event = true;
				s.phase = Phase::Packer;
				break;
			}
			case Phase::Packer:
				s.packer->step(budget);
				if (s.packer->ready()) {
					s.page = s.packer->page();
					const auto old = s.corresponding(s.page.path);
					if (old && old->digest == s.page.summary.digest &&
						old->length == s.page.summary.length && old->keys == s.page.summary.keys &&
						old->height == s.page.summary.height) {
						s.touch(*old, true);
						s.reference = *old;
						s.phase = Phase::Commit;
					} else
						s.phase = Phase::Page;
				} else if (s.packer->done()) {
					s.root.tree = s.packer->root();
					if (s.segment) {
						s.segment->seal();
						s.after_pack = Phase::Account;
						s.phase = Phase::SealPack;
					} else
						s.phase = Phase::Account;
				} else if (s.packer->accepted())
					s.phase = Phase::Visit;
				else
					return;
				break;
			case Phase::Page:
				if (s.page.summary.length > s.maximum_output_bytes - s.fresh_bytes)
					throw std::length_error("overlay output page limit exceeded");
				if (!s.segment)
					s.segment.emplace(s.account, s.storage, s.generation,
									  s.output->impl_->segments.size());
				if (!s.segment->fits(s.page)) {
					s.segment->seal();
					s.after_pack = Phase::Page;
					s.phase = Phase::SealPack;
				} else {
					s.segment->append(s.page);
					s.phase = Phase::Pack;
				}
				break;
			case Phase::Pack:
				s.segment->step(budget);
				if (!s.segment->page_ready())
					return;
				s.reference = s.segment->reference();
				s.finish_page();
				s.phase = Phase::Commit;
				break;
			case Phase::Commit:
				if (s.relocation) {
					if (!s.grant(sizeof(p::Reference), budget))
						return;
					s.frames[s.depth - 1].reference.fresh->relocation_reference = s.reference;
					s.relocated.reset();
					s.page = {};
					if (!--s.depth) {
						s.root.tree = s.reference;
						s.segment->seal();
						s.after_pack = Phase::Account;
						s.phase = Phase::SealPack;
					} else
						s.phase = Phase::Visit;
				} else {
					if (!s.packer->commit(s.reference, budget))
						return;
					s.page = {};
					s.phase = Phase::Packer;
				}
				break;
			case Phase::SealPack:
				s.segment->step(budget);
				if (!s.segment->done())
					return;
				s.phase = Phase::RegisterPack;
				break;
			case Phase::RegisterPack:
				if (!s.grant(sizeof(BuiltSegment), budget))
					return;
				{
					const auto ownership = s.segment->ownership();
					if (ownership.length > s.maximum_output_bytes - s.output_bytes)
						throw std::length_error("overlay output pack limit exceeded");
					s.output_bytes += ownership.length;
					require(s.output->impl_->segments.size() < s.limits.segments);
					s.output->impl_->segments.push_back({ownership, s.segment->bytes()});
				}
				s.phase = Phase::ReleasePack;
				break;
			case Phase::ReleasePack:
				if (!s.segment->discard(budget))
					return;
				s.segment.reset();
				s.phase = s.after_pack;
				break;
			case Phase::Account:
				if (s.position < s.extents.size()) {
					if (!s.grant(sizeof(Impl::Extent), budget))
						return;
					const auto &e = s.extents[s.position++];
					if (!e.retained) {
						const auto i = find_segment(s.inputs, e.reference.segment);
						++s.removed[i].pages;
						s.removed[i].bytes += e.reference.length;
					}
					break;
				}
				if (s.position < s.extents.size() + s.inputs.size()) {
					if (!s.grant(2 * sizeof(SegmentReachability), budget))
						return;
					const auto i = s.position++ - s.extents.size();
					require(s.live[i].pages <= s.base.pages - s.base_pages &&
							s.live[i].bytes <= s.base.bytes - s.base_bytes);
					s.base_pages += s.live[i].pages;
					s.base_bytes += s.live[i].bytes;
					require(s.removed[i].pages <= s.live[i].pages &&
							s.removed[i].bytes <= s.live[i].bytes);
					if (s.live[i].pages != s.removed[i].pages) {
						++s.reused_segments;
						s.physical_bytes += s.inputs[i].ownership.length;
					}
					s.fresh_pages += s.live[i].pages - s.removed[i].pages;
					s.fresh_bytes += s.live[i].bytes - s.removed[i].bytes;
					break;
				}
				require(s.base_pages == s.base.pages && s.base_bytes == s.base.bytes);
				s.root.pages = s.fresh_pages;
				s.root.bytes = s.fresh_bytes;
				s.root.segments = s.reused_segments + s.output->impl_->segments.size();
				require(s.output_bytes <= s.limits.bytes &&
						s.physical_bytes <= s.limits.bytes - s.output_bytes);
				s.phase = Phase::Root;
				break;
			case Phase::Root:
				if (!s.grant(3 * p::root_bytes, budget))
					return;
				{
					const auto encoded = p::encode_root(s.root, s.limits);
					auto bytes = d::make_owned_buffer(s.account, encoded.size());
					bytes->append({encoded.data(), encoded.size()});
					bytes->seal();
					auto &out = *s.output->impl_;
					out.root = s.root;
					out.bytes = std::move(bytes);
					out.descriptor = {s.generation, p::root_bytes, p::metadata_commitment(encoded),
									  s.root.cursor, s.root.profile};
					out.sealed = true;
				}
				s.phase = Phase::Done;
				break;
			case Phase::Done:
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
	return output;
}
std::span<const SegmentReachability> OverlayAssembler::removed() const {
	if (!done())
		throw std::logic_error("sparse paged assembly incomplete");
	return impl_->removed;
}
bool OverlayAssembler::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	auto &s = *impl_;
	if (!s.discarding) {
		s.discarding = true;
		s.charge = 0;
	}
	if (s.packer && !s.packer->discard(budget))
		return false;
	s.packer.reset();
	s.page = {};
	s.relocated.reset();
	if (s.segment && !s.segment->discard(budget))
		return false;
	s.segment.reset();
	if (s.output && !s.output->discard(budget))
		return false;
	s.output.reset();
	while (!s.extents.empty()) {
		if (!s.grant(sizeof(Impl::Extent), budget))
			return false;
		s.extents.pop_back();
		if (!s.extent_index.empty())
			s.extent_index.erase(s.extent_index.begin());
	}
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
