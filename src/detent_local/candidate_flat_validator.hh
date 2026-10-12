#pragma once
#include "detent_local/builder.hh"
#include "validated_flat_state.hh"
namespace detent::local::detail {
class CandidateFlatInput {
  public:
	static CandidateFlatInput capture(const ImageBuilder &);
	CandidateFlatInput(CandidateFlatInput &&) noexcept = default;
	CandidateFlatInput(const CandidateFlatInput &) = delete;

  private:
	CandidateFlatInput(std::shared_ptr<const distribution::OwnedBuffer> bytes, Descriptor descriptor)
		: bytes_(std::move(bytes)), descriptor_(descriptor) {}
	std::shared_ptr<const distribution::OwnedBuffer> bytes_;
	Descriptor descriptor_;
	friend class CandidateFlatValidator;
};
// Captures bytes and descriptor from one completed builder before validation.
class CandidateFlatValidator {
  public:
	CandidateFlatValidator(const ImageBuilder &, distribution::RetirementFactory,
						   std::shared_ptr<distribution::StorageLedger>, ViewLimits);
	CandidateFlatValidator(CandidateFlatInput, distribution::RetirementFactory,
						   std::shared_ptr<distribution::StorageLedger>, ViewLimits);
	void step(ValidationBudget &);
	bool done() const noexcept;
	ValidatedFlatState take();

  private:
	Descriptor descriptor_;
	std::optional<ImageValidator> validator_;
};
} // namespace detent::local::detail
