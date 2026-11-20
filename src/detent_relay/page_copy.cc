#include "page_copy.hh"
#include "detent_distribution/frame_input.hh"
#include "detent_local/derived_ids.hh"
#include <limits>
#include <map>
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
// The one records full copy of a body, as an agent's coordinator reads it.
class CopyInput final : public d::FrameSink {
  public:
	explicit CopyInput(const d::Limits &limits) : limits_(limits) {}
	std::optional<d::FrameInput> input;
	void frame(std::string_view name, d::FrameKind kind, std::uint64_t after, const d::NamespaceCursor &to) override {
		if (input || kind != d::FrameKind::FullCopy)
			throw std::invalid_argument("relay pages: the records answer is not one full copy");
		input.emplace(name, kind, after, to, d::Profile{}, limits_);
	}
	void piece(d::PieceKind kind, std::uint64_t first, std::uint64_t last) override { input->piece(kind, first, last); }
	void record(std::string_view key, std::uint64_t revision, const KnobDef *value) override {
		input->record(key, revision, value);
	}

  private:
	d::Limits limits_;
};
} // namespace

struct BuiltCopy::Impl {
	std::optional<x::PagedImage> image;
	std::map<local::SegmentId, std::string_view> segments;
	~Impl() {
		local::ValidationBudget disposal{std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::size_t>::max()};
		if (image)
			image->discard(disposal);
	}
};
BuiltCopy::BuiltCopy() : impl_(std::make_unique<Impl>()) {}
BuiltCopy::~BuiltCopy() = default;
std::span<const x::BuiltSegment> BuiltCopy::segments() const { return impl_->image->segments(); }
x::TransferPages BuiltCopy::pages() const {
	return [impl = impl_.get()](const p::Reference &reference) -> std::span<const char> {
		const auto found = impl->segments.find(reference.segment);
		if (found == impl->segments.end() || reference.offset > found->second.size() ||
			reference.length > found->second.size() - reference.offset)
			throw std::invalid_argument("relay pages: a reference outside the built copy");
		return {found->second.data() + reference.offset, reference.length};
	};
}
p::Limits BuiltCopy::limits() const noexcept { return page_limits(); }

std::shared_ptr<const BuiltCopy> build_copy(std::string_view body) {
	const auto limits = input_limits();
	CopyInput sink(limits);
	d::SessionFramesDecoder decoder(sink, limits);
	std::span<const char> input(body);
	auto budget = body.size();
	decoder.feed(input, budget);
	decoder.finish();
	if (!sink.input)
		throw std::invalid_argument("relay pages: the records answer holds no full copy");
	const auto at = sink.input->target();
	std::shared_ptr<const d::ValidatedSnapshot> snapshot = sink.input->snapshot();
	sink.input.reset();
	const auto bounds = page_limits();
	x::PagedSnapshotBuilder builder(std::move(snapshot), local::next_generation(std::nullopt, at, d::Profile{}, true),
									bounds);
	while (!builder.done()) {
		local::ValidationBudget step{std::size_t{1} << 20, 4096};
		builder.step(step);
	}
	std::shared_ptr<BuiltCopy> copy(new BuiltCopy);
	auto &image = copy->impl_->image.emplace(builder.take());
	for (const auto &segment : image.segments())
		copy->impl_->segments.emplace(segment.ownership.segment, segment.bytes->view());
	const auto &root = image.root();
	copy->at_ = at;
	copy->tree_ = root.tree;
	copy->page_bytes_ = root.bytes;
	const auto commitment = p::state_commitment(root);
	std::copy(commitment.begin(), commitment.end(), copy->root_.begin());
	const auto pages = copy->pages();
	const auto full = merkle_diff(root.tree, pages, {}, {}, bounds);
	copy->transfer_ = x::make_transfer(full.pages, pages, bounds);
	return copy;
}

std::vector<char> delta_transfer(const MerkleDiff &diff, const BuiltCopy &target) {
	return x::make_transfer(diff.pages, target.pages(), target.limits());
}
} // namespace detent::relay
