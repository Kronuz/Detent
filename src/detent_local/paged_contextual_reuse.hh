#pragma once
#include "leased_paged_root.hh"
namespace detent::local::detail {
class ReuseNavigator;
class ContextualReuse {
  public:
	ContextualReuse(ContextualReuse &&) noexcept = default;
	ContextualReuse(const ContextualReuse &) = delete;
	const paged::Reference &reference() const;
	const paged::PageHeader &header() const;
	std::string_view entering_prefix() const;
	const SegmentOwnership &ownership() const;
	SegmentFileIdentity file_identity() const;
	bool matches(const paged::Reference &, std::string_view entering_prefix, const Descriptor &,
				 DirectoryIdentity, StorageIdentity) const;

  private:
	ContextualReuse(distribution::RetirementHandle<LeasedPagedRoot>, paged::Reference, paged::PageHeader,
					std::string_view, const SegmentMapping &);
	distribution::RetirementHandle<LeasedPagedRoot> base_;
	paged::Reference reference_;
	paged::PageHeader header_;
	std::array<char, 202> prefix_{};
	std::size_t prefix_size_ = 0;
	SegmentOwnership ownership_;
	SegmentFileIdentity file_identity_;
	friend class ReuseNavigator;
};
// Navigation reads only checked immutable mappings; it performs no IO or hashing.
class ReuseNavigator {
  public:
	ReuseNavigator(std::shared_ptr<distribution::StorageLedger>,
				   distribution::RetirementHandle<LeasedPagedRoot>, std::string_view typed_key,
				   std::size_t frontier_depth);
	ReuseNavigator(std::shared_ptr<distribution::StorageLedger>,
				   distribution::RetirementHandle<LeasedPagedRoot>, paged::Reference,
				   std::string_view entering_prefix);
	~ReuseNavigator();
	ReuseNavigator(ReuseNavigator &&) noexcept;
	ReuseNavigator(const ReuseNavigator &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	std::optional<ContextualReuse> take();
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
