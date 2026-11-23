#include "paged_image_state.hh"
#include <algorithm>

namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
PagedImage::PagedImage(std::shared_ptr<d::StorageLedger> account, std::size_t capacity)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, capacity)) {}
PagedImage::~PagedImage() = default;
PagedImage::PagedImage(PagedImage &&) noexcept = default;
const p::Root &PagedImage::root() const {
	if (!impl_ || !impl_->sealed || impl_->discarding)
		throw std::logic_error("paged image unavailable");
	return impl_->root;
}
const Descriptor &PagedImage::descriptor() const {
	root();
	return impl_->descriptor;
}
std::span<const BuiltSegment> PagedImage::segments() const {
	root();
	return impl_->segments;
}
std::shared_ptr<const d::OwnedBuffer> PagedImage::bytes() const {
	root();
	return impl_->bytes;
}
bool PagedImage::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	impl_->discarding = true;
	while (!impl_->segments.empty() && budget.rows) {
		--budget.rows;
		impl_->segments.pop_back();
	}
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
PagedImageOwner::PagedImageOwner(PagedImage &&image) : image_(std::move(image)) {
	capacity_remaining_ = image_.bytes()->storage_capacity();
	for (const auto &segment : image_.segments())
		capacity_remaining_ += segment.bytes->storage_capacity();
}
bool PagedImageOwner::retire_step() noexcept {
	// Account backing capacity before releasing buffers in bounded row batches.
	if (capacity_remaining_) {
		capacity_remaining_ -= std::min<std::size_t>(capacity_remaining_, 8192);
		return false;
	}
	ValidationBudget budget{0, 16};
	return image_.discard(budget);
}
} // namespace detent::local::detail
