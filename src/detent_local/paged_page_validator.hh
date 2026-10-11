#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_local/paged_layout.hh"
#include "detent_local/view.hh"

namespace detent::local::detail {
// The owner retains immutable page bytes; this validates framing, not the graph.
class PageValidator {
  public:
	PageValidator(std::shared_ptr<distribution::StorageLedger>, std::shared_ptr<const void> owner,
				  std::span<const char> bytes, paged::Reference, paged::Limits = {});
	~PageValidator();
	PageValidator(PageValidator &&) noexcept;
	PageValidator(const PageValidator &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	paged::PageHeader header() const;
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
