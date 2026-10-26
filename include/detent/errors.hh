#pragma once

#include <string>

namespace detent {

// Section 5: error model. Every failure the read/update path can produce.
enum class ErrorCategory {
	Uninitialized,
	InvalidName,
	MissingKnob,
	WrongType,
	ConversionFailed,
	SourceFailure,
};

struct KnobError {
	ErrorCategory category;
	std::string name;    // knob name, when applicable
	std::string detail;  // human-readable detail; never logged with hash_val/switch_val

	[[nodiscard]] std::string what() const;
};

// Section 4/7: reasons an update/parse can be rejected. Distinct from
// KnobError, which is a *read*-path failure; this is the *update*-path one.
enum class UpdateRejectReason {
	Io,
	Transport,
	Parse,
	Schema,
	Version,   // stale/duplicate or non-positive version
	Oversize,
	Checksum,
};

struct UpdateError {
	UpdateRejectReason reason;
	std::string detail;
};

} // namespace detent
