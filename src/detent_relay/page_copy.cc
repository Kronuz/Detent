#include "page_copy.hh"
#include "detent_distribution/frame_input.hh"
#include "detent_local/derived_ids.hh"
#include "detent_local/layout.hh"
#include "paged_canonical_copy.hh"
#include "paged_delta_leaves.hh"
#include "paged_navigation.hh"
#include "paged_overlay_assembler.hh"
#include "paged_trie_overlay.hh"
#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>

namespace detent::relay {
namespace d = distribution;
namespace p = local::paged;
namespace x = local::detail;
namespace {
// A relay builds whatever namespace its mirror holds: the bounds are the format's, the memory its own.
constexpr std::size_t build_memory = std::size_t{32} << 30;
d::Limits input_limits() {
	d::Limits limits;
	limits.bytes = limits.string_bytes = std::uint64_t{64} << 30;
	limits.records = std::numeric_limits<std::uint32_t>::max();
	limits.storage_bytes = build_memory;
	limits.storage_account = std::make_shared<d::StorageLedger>(build_memory);
	return limits;
}
p::Limits page_limits() {
	p::Limits limits;
	limits.bytes = std::uint64_t{64} << 30;
	limits.pages = std::uint64_t{1} << 32;
	limits.segments = std::uint64_t{1} << 20;
	limits.records = std::numeric_limits<std::uint32_t>::max();
	return limits;
}
constexpr local::ValidationBudget unbounded{std::numeric_limits<std::size_t>::max(),
											std::numeric_limits<std::size_t>::max()};
void cancelled(const std::atomic<bool> *cancel) {
	if (cancel && cancel->load(std::memory_order_relaxed))
		throw std::runtime_error("relay pages: the build was cancelled");
}
template <class T> void dispose(T &work) {
	for (;;) {
		auto budget = unbounded;
		if (work.discard(budget))
			return;
	}
}
// The one records frame of a body, as an agent's coordinator reads it.
class RecordsInput final : public d::FrameSink {
  public:
	explicit RecordsInput(const d::Limits &limits) : limits_(limits) {}
	std::optional<d::FrameInput> input;
	void frame(std::string_view name, d::FrameKind kind, std::uint64_t after, const d::NamespaceCursor &to) override {
		if (input || kind == d::FrameKind::Pages)
			throw std::invalid_argument("relay pages: the records answer is not one records frame");
		input.emplace(name, kind, after, to, d::Profile{}, limits_);
	}
	void piece(d::PieceKind kind, std::uint64_t first, std::uint64_t last) override { input->piece(kind, first, last); }
	void record(std::string_view key, std::uint64_t revision, const KnobDef *value) override {
		input->record(key, revision, value);
	}

  private:
	d::Limits limits_;
};
// Pages and bytes of a segment whose every page is live: one a build or a relocation just wrote.
x::SegmentReachability all_live(std::span<const char> bytes) {
	x::SegmentReachability out;
	for (std::size_t at = 0; at < bytes.size(); ++out.pages) {
		const auto length = local::read_integer(bytes, at + 8, 4);
		out.bytes += length;
		at = (at + length + 7) & ~std::size_t(7);
	}
	return out;
}
} // namespace

Generation::~Generation() = default;
std::uint64_t Generation::physical_bytes() const noexcept {
	std::uint64_t total = 0;
	for (const auto &segment : segments_)
		total += segment.ownership.length;
	return total;
}
x::TransferPages Generation::pages() const {
	return [this](const p::Reference &reference) -> std::span<const char> {
		const auto i = x::find_segment(inputs_, reference.segment);
		if (i == inputs_.size() || reference.offset > inputs_[i].bytes.size() ||
			reference.length > inputs_[i].bytes.size() - reference.offset)
			throw std::invalid_argument("relay pages: a reference outside the generation");
		return inputs_[i].bytes.subspan(reference.offset, reference.length);
	};
}
p::Limits Generation::limits() const noexcept { return page_limits(); }
void Generation::hold(std::vector<std::pair<x::BuiltSegment, x::SegmentReachability>> held) {
	std::sort(held.begin(), held.end(), [](const auto &a, const auto &b) {
		return std::memcmp(a.first.ownership.segment.data(), b.first.ownership.segment.data(),
						   a.first.ownership.segment.size()) < 0;
	});
	std::uint64_t pages = 0, bytes = 0;
	for (auto &[segment, live] : held) {
		inputs_.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}, nullptr});
		live_.push_back(live);
		segments_.push_back(std::move(segment));
		pages += live.pages;
		bytes += live.bytes;
	}
	if (pages != root_.pages || bytes != root_.bytes || segments_.size() != root_.segments)
		throw std::logic_error("relay pages: a generation's segments disagree with its root");
	const auto commitment = p::state_commitment(root_);
	std::copy(commitment.begin(), commitment.end(), commitment_.begin());
}

std::shared_ptr<const Generation> build_generation(std::string_view body, std::shared_ptr<const Generation> base,
												   const std::atomic<bool> *cancel) {
	const auto limits = input_limits();
	const auto account = limits.storage_account;
	RecordsInput sink(limits);
	d::SessionFramesDecoder decoder(sink, limits);
	std::span<const char> input(body);
	auto budget = body.size();
	decoder.feed(input, budget);
	decoder.finish();
	if (!sink.input)
		throw std::invalid_argument("relay pages: the records answer holds no frame");
	const auto at = sink.input->target();
	const auto bounds = page_limits();
	std::shared_ptr<Generation> out(new Generation);
	std::vector<std::pair<x::BuiltSegment, x::SegmentReachability>> held;
	std::optional<x::PagedImage> image;
	if (!base) {
		if (sink.input->kind() != d::FrameKind::FullCopy)
			throw std::invalid_argument("relay pages: a cold build needs a full copy");
		std::shared_ptr<const d::ValidatedSnapshot> snapshot = sink.input->snapshot();
		sink.input.reset();
		x::PagedSnapshotBuilder builder(std::move(snapshot),
										local::next_generation(std::nullopt, at, d::Profile{}, true), bounds);
		while (!builder.done()) {
			cancelled(cancel);
			local::ValidationBudget step{std::size_t{1} << 20, 4096};
			builder.step(step);
		}
		image.emplace(builder.take());
	} else {
		const auto kind = sink.input->kind();
		if ((kind != d::FrameKind::Entries && kind != d::FrameKind::Blocks) ||
			sink.input->after() != base->at().position || !at.same_identity(base->at()))
			throw std::invalid_argument("relay pages: a build over a base needs the records after it");
		std::shared_ptr<const d::ValidatedDelta> delta = sink.input->delta();
		const auto name = sink.input->name();
		sink.input.reset();
		// dense-pages D7: each changed leaf through the base's trie, then the new pages assembled.
		x::DeltaLeaves leaves(name, base->at().position, delta, bounds);
		x::TrieOverlay overlay(account, base, base->inputs_, base->root_, bounds);
		for (;;) {
			cancelled(cancel);
			auto step = unbounded;
			leaves.step(step);
			if (leaves.done())
				break;
			const auto leaf = leaves.leaf();
			overlay.change(leaf.key, leaf.body);
			while (!overlay.operation_done()) {
				auto change = unbounded;
				overlay.step(change);
			}
			leaves.consume();
		}
		overlay.seal();
		x::OverlayAssembler assembler(account, overlay, base->inputs_, base->live_, base->root_,
									  local::next_generation(base->descriptor_, at, d::Profile{}, false), at, bounds);
		while (!assembler.done()) {
			cancelled(cancel);
			local::ValidationBudget step{std::size_t{1} << 20, 4096};
			assembler.step(step);
		}
		const std::vector<x::SegmentReachability> removed(assembler.removed().begin(), assembler.removed().end());
		image.emplace(assembler.take());
		for (std::size_t i = 0; i < base->segments_.size(); ++i) {
			const auto &live = base->live_[i];
			if (live.pages != removed[i].pages)
				held.push_back({base->segments_[i], {live.pages - removed[i].pages, live.bytes - removed[i].bytes}});
		}
		dispose(assembler);
		dispose(overlay);
		dispose(leaves);
	}
	out->root_ = image->root();
	out->descriptor_ = image->descriptor();
	for (const auto &segment : image->segments())
		held.push_back({segment, all_live(segment.bytes->view())});
	dispose(*image);
	out->hold(std::move(held));
	return out;
}

std::shared_ptr<const Generation> relocate(const Generation &generation, const std::atomic<bool> *cancel) {
	cancelled(cancel);
	const auto bounds = page_limits();
	const auto name = local::next_generation(std::nullopt, generation.at(), d::Profile{}, true);
	auto copy = x::canonical_copy(input_limits().storage_account, generation.inputs_, generation.root_, name.id, bounds);
	std::shared_ptr<Generation> out(new Generation);
	out->root_ = copy.root;
	const auto encoded = p::encode_root(copy.root, bounds);
	out->descriptor_ = {name.id, p::root_bytes, p::metadata_commitment(encoded), copy.root.cursor, copy.root.profile,
						name.ordinal};
	std::vector<std::pair<x::BuiltSegment, x::SegmentReachability>> held;
	for (auto &segment : copy.segments) {
		const auto live = all_live(segment.bytes->view());
		held.push_back({std::move(segment), live});
	}
	out->hold(std::move(held));
	return out;
}

std::shared_ptr<const CanonicalCopy> full_copy(const Generation &canonical) {
	auto out = std::make_shared<CanonicalCopy>();
	out->at = canonical.at();
	out->tree = canonical.tree();
	out->root = canonical.root();
	const auto pages = canonical.pages();
	const auto full = merkle_diff(canonical.tree(), pages, {}, {}, canonical.limits());
	out->transfer = x::make_transfer(full.pages, pages, canonical.limits());
	return out;
}

WindowGeneration window_entry(std::shared_ptr<const Generation> generation) {
	WindowGeneration out{generation->at().position, generation->tree(), generation->pages(), {}, {}};
	for (const auto &segment : generation->segments())
		out.segments.emplace_back(segment.ownership.segment, segment.ownership.length);
	out.owner = std::move(generation);
	return out;
}

std::vector<char> delta_transfer(const MerkleDiff &diff, const Generation &target) {
	return x::make_transfer(diff.pages, target.pages(), target.limits());
}
} // namespace detent::relay
