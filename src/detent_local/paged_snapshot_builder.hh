#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_distribution/protocol.hh"
#include "detent_local/generation.hh"
#include "detent_local/ownership.hh"
#include "detent_local/paged_layout.hh"

namespace detent::local::detail {
struct BuiltSegment {
	SegmentOwnership ownership;
	std::shared_ptr<const distribution::OwnedBuffer> bytes;
};
class PagedImage;
class PagedSnapshotBuilder {
  public:
	// Retain decoded input separately for its budgeted disposal after this builder.
	PagedSnapshotBuilder(std::shared_ptr<const distribution::ValidatedSnapshot>, StorageIdentity,
						 GenerationId, paged::Limits = {});
	~PagedSnapshotBuilder();
	PagedSnapshotBuilder(PagedSnapshotBuilder &&) noexcept;
	PagedSnapshotBuilder(const PagedSnapshotBuilder &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	PagedImage take();
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
// Newly built segments only; publication retains any reused dependencies separately.
class PagedImage {
  public:
	~PagedImage();
	PagedImage(PagedImage &&) noexcept;
	PagedImage(const PagedImage &) = delete;
	const paged::Root &root() const;
	const Descriptor &descriptor() const;
	std::span<const BuiltSegment> segments() const;
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const;
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	PagedImage(std::shared_ptr<distribution::StorageLedger>, std::size_t capacity);
	distribution::StorageOwner<Impl> impl_;
	friend class PagedSnapshotBuilder;
	friend class OverlayAssembler;
};
// Construction buffers leave owner executors through the cleanup runtime.
class PagedImageOwner final : public distribution::RetirementNode {
  public:
	explicit PagedImageOwner(PagedImage &&);
	const PagedImage &image() const noexcept { return image_; }

  protected:
	bool retire_step() noexcept override;

  private:
	PagedImage image_;
	std::size_t capacity_remaining_ = 0;
};
} // namespace detent::local::detail
