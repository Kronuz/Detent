#pragma once
#include "detent_local/generation.hh"
#include "detent_local/ownership.hh"
#include "paged_page_builder.hh"

namespace detent::local::detail {
class SegmentBuilder {
  public:
	SegmentBuilder(std::shared_ptr<distribution::StorageLedger>, StorageIdentity storage,
				   GenerationId publication, std::uint64_t ordinal);
	~SegmentBuilder();
	SegmentBuilder(SegmentBuilder &&) noexcept;
	SegmentBuilder(const SegmentBuilder &) = delete;
	bool fits(const PageBuilder &) const;
	void append(const PageBuilder &);
	void step(ValidationBudget &);
	bool page_ready() const noexcept;
	paged::Reference reference() const;
	void seal();
	bool done() const noexcept;
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const;
	SegmentOwnership ownership() const;
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
