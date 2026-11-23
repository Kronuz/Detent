#include "paged_transfer_plan.hh"
#include "detent_local/derived_ids.hh"
#include "detent_local/layout.hh"
#include "paged_image_state.hh"
#include "paged_navigation.hh"
#include "paged_node_step.hh"
#include "scrub_source.hh"
#include <set>

namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
namespace {
void within(bool ok) {
	if (!ok)
		throw TransferRefused(TransferRefusal::Limits, "delta transfer over the generation's limits");
}
template <class Visit> void each_external(std::span<const char> page, p::Limits limits, Visit visit) {
	const auto header = p::decode_page_header(page, limits);
	for (std::uint16_t j = 0; j < header.externals; ++j)
		visit(p::decode_external(page, j, limits));
}
std::uint64_t namespace_leaves(std::span<const char> page, p::Limits limits) {
	const auto header = p::decode_page_header(page, limits);
	const auto end = page.size() - p::child_bytes * header.externals;
	std::uint64_t out = 0;
	for (p::NodeAt at; at.offset() < end;) {
		const auto node = at.decode(page, limits);
		out += node.kind == p::PageKind::Namespace;
		at = p::NodeAt::after(node);
	}
	return out;
}
} // namespace

struct PagedTransferPlanBuilder::Work final : d::RetirementNode {
	SparsePlanFinalizer finalizer;
	bool failed = false;
	Work(d::RetirementHandle<PagedPlanState> plan, std::shared_ptr<d::StorageLedger> heap, p::Limits limits)
		: finalizer(std::move(plan), std::move(heap), limits) {}
	bool retire_step() noexcept override {
		ValidationBudget budget{8192, 16};
		return finalizer.discard(budget);
	}
};

PagedTransferPlanBuilder::PagedTransferPlanBuilder(const RecoveredGeneration &base, ReceivedTransfer &&received,
												   const d::NamespaceCursor &target, d::RetirementFactory factory,
												   std::shared_ptr<d::StorageLedger> heap, p::Limits limits) {
	auto leased = ScrubSource::root(base);
	const auto &descriptor = base.descriptor();
	if (!leased || !factory.uses_account(heap) || !target.same_identity(descriptor.cursor) ||
		target.position <= descriptor.cursor.position)
		throw std::invalid_argument("a delta transfer moves its paged base forward with exact resources");
	if (!received.pages)
		throw TransferRefused(TransferRefusal::Empty, "a delta transfer with no pages");
	plan_ = factory.make<PagedPlanState>(heap, leased, PagedPlanState::PhysicalMaintenance{});
	plan_->derive(target, descriptor.profile);
	const DerivedGeneration name{plan_->descriptor_.generation, plan_->descriptor_.ordinal};
	const auto ids = transfer_segment_ids(name);
	std::set<SegmentId> fresh;
	for (std::size_t s = 0; s < received.segments.size(); ++s) {
		if (received.segments[s].ownership.segment != ids(std::uint32_t(s + 1)))
			throw std::invalid_argument("received segments are not named by the delta's generation");
		fresh.insert(received.segments[s].ownership.segment);
	}
	// The base pages the new tree reuses: the received pages' children outside the received packs.
	std::set<std::pair<SegmentId, std::uint32_t>> reused;
	for (const auto &segment : received.segments) {
		const std::span<const char> bytes(segment.bytes->data(), segment.bytes->size());
		for (std::size_t at = 0; at < bytes.size();) {
			const auto page = bytes.subspan(at, read_integer(bytes, at + 8, 4));
			each_external(page, limits, [&](const p::Reference &child) {
				if (!fresh.contains(child.segment))
					reused.insert({child.segment, child.offset});
			});
			at = (at + page.size() + 7) & ~std::size_t(7);
		}
	}
	// The base pages it no longer reaches: the base tree from its root, stopping at reused pages.
	const auto &prior = leased->root();
	const auto inputs = leased->registry()->inputs();
	std::vector<SegmentReachability> removed(inputs.size());
	std::uint64_t removed_pages = 0, removed_bytes = 0, removed_namespaces = 0;
	std::vector<p::Reference> pending;
	if (prior.pages)
		pending.push_back(prior.tree);
	while (!pending.empty()) {
		const auto reference = pending.back();
		pending.pop_back();
		if (reused.contains({reference.segment, reference.offset}))
			continue;
		const auto i = find_segment(inputs, reference.segment);
		if (i == inputs.size())
			throw ReadSourceFailure("base lacks a segment its tree reaches");
		const auto page = checked_page_bytes(inputs[i], reference);
		++removed[i].pages;
		removed[i].bytes += reference.length;
		++removed_pages;
		removed_bytes += reference.length;
		removed_namespaces += namespace_leaves(page, limits);
		each_external(page, limits, [&](const p::Reference &child) { pending.push_back(child); });
	}
	within(removed_pages <= prior.pages && removed_bytes <= prior.bytes && removed_namespaces <= prior.namespaces);
	auto &registry = *leased->registry();
	std::uint64_t segments = received.segments.size();
	for (std::size_t i = 0; i < inputs.size(); ++i)
		segments += registry.reachability(i).pages > removed[i].pages;
	const auto namespaces = prior.namespaces - removed_namespaces + received.namespaces;
	// The namespace's one watermark stays: a delta that drops or adds one is not this namespace's.
	if (namespaces != 1)
		throw TransferRefused(TransferRefusal::Leaf, "a delta transfer without its namespace's one leaf");
	within(received.tree.keys >= namespaces && namespaces <= limits.namespaces &&
		   received.tree.keys - namespaces <= limits.records && segments <= limits.segments &&
		   prior.pages - removed_pages + received.pages <= limits.pages &&
		   received.bytes <= limits.bytes - (prior.bytes - removed_bytes));

	PagedImage image(heap, received.segments.size());
	auto &out = *image.impl_;
	out.root.cursor = target;
	out.root.profile = descriptor.profile;
	out.root.pages = prior.pages - removed_pages + received.pages;
	out.root.bytes = prior.bytes - removed_bytes + received.bytes;
	out.root.segments = segments;
	out.root.namespaces = std::uint32_t(namespaces);
	out.root.records = std::uint32_t(received.tree.keys - namespaces);
	out.root.tree = received.tree;
	for (auto &segment : received.segments)
		out.segments.push_back({segment.ownership, std::move(segment.bytes)});
	const auto encoded = p::encode_root(out.root, limits);
	auto bytes = d::make_owned_buffer(heap, encoded.size());
	bytes->append({encoded.data(), encoded.size()});
	bytes->seal();
	out.descriptor = {name.id, p::root_bytes, p::metadata_commitment(encoded), target, descriptor.profile, name.ordinal};
	out.bytes = std::move(bytes);
	out.sealed = true;
	work_ = factory.make<Work>(plan_, heap, limits);
	work_->finalizer.start(std::move(image), std::move(removed));
}
PagedTransferPlanBuilder::~PagedTransferPlanBuilder() = default;
PagedTransferPlanBuilder::PagedTransferPlanBuilder(PagedTransferPlanBuilder &&) noexcept = default;
void PagedTransferPlanBuilder::step(ValidationBudget &budget) {
	if (!work_ || !plan_ || work_->failed)
		throw std::logic_error("inactive paged transfer plan builder");
	try {
		work_->finalizer.step(budget);
		if (work_->finalizer.done())
			plan_->sealed_ = true;
	} catch (...) {
		work_->failed = true;
		throw;
	}
}
bool PagedTransferPlanBuilder::done() const noexcept { return work_ && plan_ && !work_->failed && plan_->sealed_; }
PagedPlan PagedTransferPlanBuilder::take() {
	if (!done())
		throw std::logic_error("paged transfer plan incomplete");
	work_.reset();
	return PagedPlan(std::move(plan_));
}
bool PagedTransferPlanBuilder::discard(ValidationBudget &budget) {
	if (!work_ && !plan_)
		return true;
	if (!budget.rows)
		return false;
	--budget.rows;
	work_.reset();
	plan_.reset();
	return true;
}
} // namespace detent::local::detail
