#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_local/paged_layout.hh"
#include "detent_local/view.hh"

namespace detent::local::detail {
// Borrowed inputs remain immutable until sealing or worker-side disposal.
class PageBuilder {
  public:
	PageBuilder(std::shared_ptr<distribution::StorageLedger>, paged::PageKind, std::uint64_t keys,
				std::string_view prefix, std::span<const paged::Child>, std::span<const char> body);
	~PageBuilder();
	PageBuilder(PageBuilder &&) noexcept;
	PageBuilder(const PageBuilder &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const;
	paged::Digest logical() const;
	paged::Digest physical() const;
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
