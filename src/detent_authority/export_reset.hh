#pragma once
#include "detent_distribution/delta_encoder.hh"
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
using DeltaRepresentationLimit = distribution::DeltaRepresentationLimit;
} // namespace detent::authority
