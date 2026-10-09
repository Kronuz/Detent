#pragma once
#include <stdexcept>

namespace detent::authority {
class SnapshotRequired final : public std::invalid_argument {
  public:
	enum class Reason { HistoryLost, RepresentationLimit };
	explicit SnapshotRequired(Reason reason)
		: std::invalid_argument(reason == Reason::HistoryLost ? "distribution history continuity lost"
															  : "distribution delta representation limit"),
		  reason_(reason) {}
	Reason reason() const noexcept { return reason_; }

  private:
	Reason reason_;
};
class DeltaRepresentationLimit final : public std::length_error {
  public:
	using std::length_error::length_error;
};
} // namespace detent::authority
