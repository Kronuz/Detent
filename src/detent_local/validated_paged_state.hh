#pragma once
#include "detent_local/generation.hh"
#include "detent_local/paged_layout.hh"
namespace detent::local::detail {
class CandidatePagedValidator;
// Checked construction bytes carry semantic evidence without a recovered disk lease.
class ValidatedPagedState {
  public:
	const Descriptor &descriptor() const noexcept { return descriptor_; }
	const ReadView &view() const noexcept { return view_; }
	const paged::Root &root() const noexcept { return root_; }

  private:
	ValidatedPagedState(ReadView view, Descriptor descriptor, paged::Root root)
		: view_(std::move(view)), descriptor_(descriptor), root_(root) {}
	ReadView view_;
	Descriptor descriptor_;
	paged::Root root_;
	friend class CandidatePagedValidator;
};
} // namespace detent::local::detail
