#pragma once
#include "delta_export.hh"
#include "detent_distribution/framing.hh"
#include "export_encoder.hh"
namespace detent::authority {
// One authority-owner slot, retained through capture retirement. Transport may
// receive only take_frame() results, never this session or its projection.
class DeliveryExport {
  public:
	DeliveryExport(const State &, std::shared_ptr<const AuthorizedScope>, std::shared_ptr<ChangeHistory>,
				   std::optional<distribution::Cursor> base, distribution::Identity update, bool compress,
				   distribution::Limits = {});
	~DeliveryExport();
	void step(distribution::WorkBudget &);
	bool counted() const noexcept;
	std::uint64_t encoded_length() const;
	distribution::UpdateKind kind() const;
	const distribution::Cursor &target() const noexcept;
	// Established on the owner at snapshot capture, before any frame is exposed.
	std::optional<ChangeHistory::Handoff> history_handoff() const noexcept;
	bool frame_ready() const noexcept;
	std::shared_ptr<const distribution::OwnedBuffer> take_frame();
	bool done() const noexcept;
	bool discard(distribution::WorkBudget &, CommandWorkBudget &) noexcept;

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::authority
