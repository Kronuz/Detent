#pragma once
#include "detent_local/generation.hh"
#include "flat_state_rows.hh"
namespace detent::local::detail {
class CandidateFlatValidator;
// Construction evidence never supplies recovered filesystem or publication-base authority.
class ValidatedFlatState {
  public:
	const Descriptor &descriptor() const noexcept { return descriptor_; }
	const FlatStateRows &rows() const noexcept { return rows_; }

  private:
	ValidatedFlatState(ReadView view, Descriptor descriptor)
		: rows_(std::move(view)), descriptor_(descriptor) {}
	FlatStateRows rows_;
	Descriptor descriptor_;
	friend class CandidateFlatValidator;
};
} // namespace detent::local::detail
