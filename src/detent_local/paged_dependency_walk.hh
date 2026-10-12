#pragma once
#include "paged_graph_validator.hh"
#include <optional>
namespace detent::local::detail {
// Enumerates checked references; GraphValidator still certifies full semantics.
class PagedDependencyWalk {
  public:
	using Lookup = std::optional<SegmentInput> (*)(const void *, const StorageIdentity &);
	using Frontier = bool (*)(const void *, const paged::Reference &);
	PagedDependencyWalk(std::shared_ptr<distribution::StorageLedger>, std::shared_ptr<const void> source,
						Lookup, paged::Root, StorageIdentity storage, paged::Limits = {}, Frontier = nullptr);
	~PagedDependencyWalk();
	PagedDependencyWalk(PagedDependencyWalk &&) noexcept;
	PagedDependencyWalk(const PagedDependencyWalk &) = delete;
	void step(ValidationBudget &);
	std::optional<StorageIdentity> missing() const;
	bool done() const noexcept;
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
