#include "detent/read_view.hh"
#include "detent/detail/evaluation.hh"
#include "detent/validation.hh"

namespace detent {
namespace {
KnobError error(ErrorCategory category, std::string_view name) {
	return {category, std::string(name), "scoped lookup failed"};
}
} // namespace
ClientReadView::ClientReadView(std::shared_ptr<const Snapshot> snapshot,
							   std::shared_ptr<const OverrideTable> overrides) noexcept
	: snapshot_(std::move(snapshot)), overrides_(std::move(overrides)) {}
ClientReadView::ClientReadView(std::shared_ptr<const EvaluationView> evaluator) noexcept
	: evaluator_(std::move(evaluator)) {}
Result<std::int64_t, KnobError> ClientReadView::get(std::string_view name, std::optional<std::string_view> hash,
													std::optional<std::string_view> group) const {
	if (!is_valid_knob_name(name))
		return error(ErrorCategory::InvalidName, name);
	if (evaluator_)
		return evaluator_->get(name, hash, group);
	if (overrides_) {
		auto found = overrides_->values.find(name);
		if (found != overrides_->values.end())
			return found->second;
	}
	if (!snapshot_)
		return error(ErrorCategory::Uninitialized, name);
	auto found = snapshot_->knobs.find(name);
	if (found == snapshot_->knobs.end())
		return error(ErrorCategory::MissingKnob, name);
	return detail::evaluate(found->second, name, hash, group);
}
std::optional<distribution::Cursor> ClientReadView::cursor() const noexcept {
	return evaluator_ ? evaluator_->cursor() : std::nullopt;
}
} // namespace detent
