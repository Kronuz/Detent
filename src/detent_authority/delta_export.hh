#pragma once
#include "change_history.hh"
#include "detent_distribution/delta_encoder.hh"
#include "detent_distribution/protocol.hh"
#include "export_reset.hh"
#include "export_scope.hh"
namespace detent::authority {
// Owner-local encoder. History refusal throws before any chunk is exposed;
// callers then capture a snapshot. Sealed chunks alone cross to transport.
class DeltaExportEncoder {
  public:
	DeltaExportEncoder(std::shared_ptr<ChangeHistory> history, std::shared_ptr<const AuthorizedScope> scope,
					   const Context &context, distribution::Cursor base, distribution::Cursor target,
					   distribution::Limits limits = {},
					   std::optional<ChangeHistory::Handoff> handoff = std::nullopt);
	~DeltaExportEncoder();
	void step(distribution::WorkBudget &);
	bool counted() const noexcept;
	std::uint64_t encoded_length() const;
	distribution::UpdateKind kind() const;
	bool done() const noexcept;
	bool chunk_ready() const noexcept;
	std::shared_ptr<const distribution::OwnedBuffer> take_chunk();
	bool discard(distribution::WorkBudget &) noexcept;

  private:
	distribution::DeltaEncoder encoder_;
};
} // namespace detent::authority
