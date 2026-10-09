#pragma once
#include "detent_distribution/framing.hh"
#include <optional>
namespace detent::delivery {
struct ReceivedUpdate {
	distribution::UpdateKind kind;
	distribution::Cursor target;
	std::shared_ptr<distribution::ValidatedSnapshot> snapshot;
	std::shared_ptr<distribution::ValidatedDelta> delta;
};
// Network-owner decoder. HTTP, frame integrity and schema completion must all
// succeed before a candidate escapes. A caller retains each original read
// buffer until feed() consumes it; step() needs no additional network bytes.
class ResponseDecoder {
  public:
	ResponseDecoder(distribution::Cursor configured, std::optional<distribution::Cursor> base,
					distribution::NamespaceSelection namespaces, bool compression, distribution::Limits = {});
	~ResponseDecoder();
	void feed(std::span<const char> &, distribution::WorkBudget &);
	void step(distribution::WorkBudget &);
	bool needs_input() const noexcept;
	bool done() const noexcept;
	ReceivedUpdate take();
	// EOF before HTTP message completion is always a failure.
	void eof();
	bool discard(distribution::WorkBudget &) noexcept;

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::delivery
