#include "candidate_paged_validator.hh"
#include "paged_navigation.hh"
#include <cstring>
namespace detent::local::detail {
namespace d = distribution;
namespace {
d::RetirementHandle<PagedImageOwner> adopt(PagedImage &&image, d::RetirementFactory factory,
										   const std::shared_ptr<d::StorageLedger> &heap,
										   StorageIdentity storage, ViewLimits limits) {
	if (!heap || !factory.uses_account(heap) || limits.bytes < paged::root_bytes ||
		!std::any_of(storage.begin(), storage.end(), [](char value) { return value != 0; }))
		throw std::invalid_argument("paged candidate requires exact resources");
	const auto bytes = image.bytes();
	const auto descriptor = image.descriptor();
	const auto root = image.root();
	if (!bytes || !bytes->sealed() || bytes->account() != heap || bytes->size() != paged::root_bytes ||
		descriptor.length != bytes->size() || descriptor.cursor != root.cursor ||
		descriptor.profile != root.profile || root.bytes > limits.bytes - descriptor.length ||
		root.records > limits.records || root.namespaces > limits.namespaces ||
		image.segments().size() != root.segments || root.segments > paged::Limits{}.segments)
		throw std::invalid_argument("paged candidate manifest mismatch");
	const auto encoded = paged::encode_root(root);
	if (!std::equal(encoded.begin(), encoded.end(), bytes->data()))
		throw std::invalid_argument("paged candidate bytes differ from manifest");
	std::uint64_t physical = 0;
	for (const auto &segment : image.segments()) {
		if (!segment.bytes || segment.bytes->account() != heap || segment.ownership.storage != storage ||
			segment.ownership.length > limits.bytes - descriptor.length - physical)
			throw std::invalid_argument("paged candidate pack ownership or extent mismatch");
		physical += segment.ownership.length;
	}
	return factory.make<PagedImageOwner>(std::move(image));
}
} // namespace
CandidatePagedValidator::CandidatePagedValidator(PagedImage &&image, d::RetirementFactory factory,
												 std::shared_ptr<d::StorageLedger> heap,
												 StorageIdentity storage, ViewLimits limits)
	: factory_(factory), heap_(std::move(heap)), limits_(limits),
	  image_(adopt(std::move(image), factory_, heap_, storage, limits_)),
	  inputs_(factory_, heap_, storage, image_->image().root().segments,
			  limits_.bytes - image_->image().descriptor().length),
	  storage_(storage) {
	limits_.retirement_domain = nullptr;
}
void CandidatePagedValidator::step(ValidationBudget &budget) {
	if (!manifest_checked_) {
		const auto bytes = image_->image().bytes();
		const auto amount = std::min(bytes->size() - hashed_, budget.bytes);
		hash_.add(bytes->data() + hashed_, amount);
		hashed_ += amount;
		budget.bytes -= amount;
		if (hashed_ != bytes->size() || !budget.rows)
			return;
		--budget.rows;
		unsigned char digest[32];
		hash_.getHash(digest);
		if (std::memcmp(digest, image_->image().descriptor().checksum.data(), 32) != 0)
			throw std::invalid_argument("paged candidate manifest checksum mismatch");
		manifest_checked_ = true;
	}
	while (!graph_) {
		if (inputs_.busy()) {
			inputs_.step(budget);
			if (inputs_.busy())
				return;
		}
		const auto segments = image_->image().segments();
		if (next_ == segments.size()) {
			inputs_.freeze();
			paged::Limits limits;
			limits.bytes = limits_.bytes - image_->image().descriptor().length;
			limits.records = limits_.records;
			limits.namespaces = limits_.namespaces;
			graph_.emplace(heap_, inputs_.owner(), inputs_.inputs(), image_->image().root(), storage_, limits,
						   PagedSemanticLimits{limits_.strings, limits_.overrides});
			break;
		}
		if (!budget.rows)
			return;
		--budget.rows;
		const auto &segment = segments[next_++];
		inputs_.begin(segment.ownership, segment.bytes);
	}
	graph_->step(budget);
}
bool CandidatePagedValidator::done() const noexcept { return graph_ && graph_->done(); }
PagedStateCounts CandidatePagedValidator::counts() const {
	if (!done())
		throw std::logic_error("paged candidate validation incomplete");
	return graph_->counts();
}
SegmentReachability CandidatePagedValidator::segment_reachability(std::size_t index) const {
	if (!done())
		throw std::logic_error("paged candidate validation incomplete");
	return graph_->segment_reachability(index);
}
SegmentReachability CandidatePagedValidator::segment_reachability(const StorageIdentity &identity) const {
	if (!done())
		throw std::logic_error("paged candidate validation incomplete");
	const auto inputs = inputs_.inputs();
	const auto index = find_segment(inputs, identity);
	if (index == inputs.size())
		throw std::invalid_argument("candidate segment is absent");
	return graph_->segment_reachability(index);
}
std::span<const std::uint32_t, 257> CandidatePagedValidator::override_histogram() const {
	if (!done())
		throw std::logic_error("paged candidate validation incomplete");
	return graph_->override_histogram();
}
ValidatedPagedState CandidatePagedValidator::take() {
	if (!done())
		throw std::logic_error("paged candidate validation incomplete");
	auto view = graph_->take(factory_, heap_);
	return ValidatedPagedState(std::move(view), image_->image().descriptor(), image_->image().root());
}
} // namespace detent::local::detail
