#include "candidate_flat_validator.hh"
namespace detent::local::detail {
CandidateFlatInput CandidateFlatInput::capture(const ImageBuilder &builder) {
	if (!builder.done())
		throw std::invalid_argument("candidate capture requires a completed builder");
	return CandidateFlatInput(builder.bytes(), builder.descriptor());
}
CandidateFlatValidator::CandidateFlatValidator(const ImageBuilder &builder,
											   distribution::RetirementFactory factory,
											   std::shared_ptr<distribution::StorageLedger> heap,
											   ViewLimits limits)
	: CandidateFlatValidator(CandidateFlatInput::capture(builder), std::move(factory), std::move(heap),
							 limits) {}
CandidateFlatValidator::CandidateFlatValidator(CandidateFlatInput input,
											   distribution::RetirementFactory factory,
											   std::shared_ptr<distribution::StorageLedger> heap,
											   ViewLimits limits) {
	if (!heap || !factory.uses_account(heap))
		throw std::invalid_argument("candidate validation requires exact resources");
	descriptor_ = input.descriptor_;
	auto bytes = std::move(input.bytes_);
	if (!bytes || bytes->account() != heap || bytes->size() != descriptor_.length)
		throw std::invalid_argument("candidate builder ownership mismatch");
	limits.retirement_domain = nullptr;
	validator_.emplace(ImageValidator(bytes, {bytes->data(), bytes->size()}, limits, {}, std::move(factory),
									  std::move(heap)));
}
void CandidateFlatValidator::step(ValidationBudget &budget) {
	if (!validator_)
		throw std::logic_error("candidate validator consumed");
	validator_->step(budget);
}
bool CandidateFlatValidator::done() const noexcept { return validator_ && validator_->done(); }
ValidatedFlatState CandidateFlatValidator::take() {
	if (!done())
		throw std::logic_error("candidate validation incomplete");
	auto view = validator_->take();
	validator_.reset();
	ValidatedFlatState state(std::move(view), descriptor_);
	const auto &header = state.rows().header();
	if (header.layout.length != descriptor_.length || header.cursor != descriptor_.cursor ||
		header.profile != descriptor_.profile)
		throw std::invalid_argument("candidate descriptor differs from validated state");
	return state;
}
} // namespace detent::local::detail
