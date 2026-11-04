#include "detent/errors.hh"

namespace detent {
namespace {
const char *category_name(ErrorCategory category) noexcept {
	switch (category) {
	case ErrorCategory::Uninitialized: return "Uninitialized";
	case ErrorCategory::InvalidName: return "InvalidName";
	case ErrorCategory::MissingKnob: return "MissingKnob";
	case ErrorCategory::WrongType: return "WrongType";
	case ErrorCategory::ConversionFailed: return "ConversionFailed";
	case ErrorCategory::SourceFailure: return "SourceFailure";
	}
	return "Unknown";
}
} // namespace

std::string KnobError::what() const {
	std::string s = category_name(category);
	s += ": ";
	s += name;
	if (!detail.empty()) {
		s += " (";
		s += detail;
		s += ")";
	}
	return s;
}
} // namespace detent
