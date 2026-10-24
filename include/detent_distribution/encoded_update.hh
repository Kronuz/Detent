#pragma once
#include "delta_encoder.hh"
#include "retirement.hh"
#include "snapshot_encoder.hh"

namespace detent::distribution {
inline constexpr std::uint32_t encoded_update_fragment_limit = 65536;
class UpdateStage;
// Independent wire bytes. Final handle release queues incremental retirement.
class EncodedUpdate final : public RetirementNode {
  public:
	EncodedUpdate(Cursor base, Cursor target, UpdateKind, Identity update, std::uint64_t length,
				  std::shared_ptr<StorageLedger>);
	const Cursor &base() const noexcept { return base_; }
	const Cursor &target() const noexcept { return target_; }
	UpdateKind kind() const noexcept { return kind_; }
	const Identity &identity() const noexcept { return update_; }
	std::uint64_t logical_bytes() const noexcept { return length_; }
	bool sealed() const noexcept { return sealed_; }
	std::size_t fragments() const noexcept { return sealed_ ? raw_.size() : 0; }
	std::shared_ptr<const OwnedBuffer> frame(std::size_t ordinal, bool compressed) const;

  protected:
	bool retire_step() noexcept override;

  private:
	using Frames =
		std::vector<std::shared_ptr<const OwnedBuffer>, StorageAllocator<std::shared_ptr<const OwnedBuffer>>>;
	Cursor base_, target_;
	UpdateKind kind_;
	Identity update_;
	std::uint64_t length_;
	Frames raw_, compressed_;
	bool sealed_ = false;
	friend class UpdateStage;
};
// Staging is owner-local. Completion releases semantic sources before exposing
// sealed bytes; dropping an unfinished stage queues bounded worker cleanup.
class UpdateStage final : public RetirementNode {
  public:
	UpdateStage(RetirementDomain &, std::shared_ptr<SnapshotSource>, Identity, Limits = {});
	UpdateStage(RetirementDomain &, std::shared_ptr<DeltaSource>, Identity, Limits = {});
	~UpdateStage();
	void step(WorkBudget &);
	bool done() const noexcept;
	std::uint64_t emitted_logical_bytes() const noexcept;
	RetirementHandle<EncodedUpdate> take();
	bool abort(WorkBudget &) noexcept;

  protected:
	bool retire_step() noexcept override;

  private:
	struct Impl;
	StorageOwner<Impl> impl_;
};
} // namespace detent::distribution
