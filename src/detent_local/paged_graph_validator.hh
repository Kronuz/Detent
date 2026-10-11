#pragma once
#include "detent_local/ownership.hh"
#include "paged_page_validator.hh"

namespace detent::local::detail {
struct SegmentInput {
	SegmentOwnership ownership;
	std::span<const char> bytes;
};
// The owner retains the immutable, identity-sorted registry and segment bytes.
class GraphValidator {
  public:
	GraphValidator(std::shared_ptr<distribution::StorageLedger>, std::shared_ptr<const void> owner,
				   std::span<const SegmentInput>, paged::Root, StorageIdentity, paged::Limits = {});
	~GraphValidator();
	GraphValidator(GraphValidator &&) noexcept;
	GraphValidator(const GraphValidator &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	paged::Root root() const;
	ReadView take(distribution::RetirementDomain &);
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
