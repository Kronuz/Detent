#pragma once
#include "leased_paged_root.hh"
namespace detent::local::detail {
struct PagedLeaf {
	paged::Reference reference;
	paged::PageHeader header;
	std::string_view key;
	std::span<const char> body;
};
// Lexical traversal borrows verified mappings and retains their actual root lease.
class PagedCursor {
  public:
	PagedCursor(std::shared_ptr<distribution::StorageLedger>, const RecoveredGeneration &, paged::PageKind);
	PagedCursor(std::shared_ptr<distribution::StorageLedger>, distribution::RetirementHandle<LeasedPagedRoot>,
				paged::PageKind);
	~PagedCursor();
	PagedCursor(PagedCursor &&) noexcept;
	PagedCursor(const PagedCursor &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	bool ready() const noexcept;
	PagedLeaf leaf() const;
	void consume();
	void rewind();
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
