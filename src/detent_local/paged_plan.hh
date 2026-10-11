#pragma once
#include "detent_distribution/buffer.hh"
#include "paged_reuse.hh"
#include <sha256.h>
namespace detent::local::detail {
class PagedPlanState final : public distribution::RetirementNode {
  public:
	PagedPlanState(std::shared_ptr<distribution::StorageLedger>,
				   distribution::RetirementHandle<LeasedPagedRoot>, GenerationId,
				   const distribution::ValidatedDelta &);
	const Descriptor &descriptor() const;
	const paged::Root &root() const;
	const Descriptor &base_descriptor() const;
	DirectoryIdentity directory_identity() const;
	StorageIdentity storage_identity() const;
	const distribution::RetirementHandle<SegmentRegistry> &registry() const;
	PagedStateCounts counts() const;
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const;

  protected:
	bool retire_step() noexcept override;

  private:
	distribution::RetirementHandle<LeasedPagedRoot> base_;
	distribution::RetirementHandle<SegmentRegistry> registry_;
	paged::Root root_;
	Descriptor descriptor_;
	PagedStateCounts counts_;
	std::shared_ptr<distribution::OwnedBuffer> bytes_;
	std::array<char, paged::root_bytes> encoded_{};
	std::size_t copied_ = 0, hashed_ = 0;
	SHA256 hash_;
	bool sealed_ = false;
	friend class PagedPlanBuilder;
};
// Move-only exact-base publication authority; retirement never runs on its caller.
class PagedPlan {
  public:
	PagedPlan(PagedPlan &&) noexcept = default;
	PagedPlan(const PagedPlan &) = delete;
	const Descriptor &descriptor() const { return checked_state().descriptor(); }
	const paged::Root &root() const { return checked_state().root(); }
	const Descriptor &base_descriptor() const { return checked_state().base_descriptor(); }
	DirectoryIdentity directory_identity() const { return checked_state().directory_identity(); }
	StorageIdentity storage_identity() const { return checked_state().storage_identity(); }
	const distribution::RetirementHandle<SegmentRegistry> &registry() const {
		return checked_state().registry();
	}
	PagedStateCounts counts() const { return checked_state().counts(); }
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const { return checked_state().bytes(); }

  private:
	const PagedPlanState &checked_state() const {
		if (!state_)
			throw std::logic_error("moved paged publication plan");
		return *state_;
	}
	explicit PagedPlan(distribution::RetirementHandle<PagedPlanState> state) : state_(std::move(state)) {}
	distribution::RetirementHandle<PagedPlanState> state_;
	friend class PagedPlanBuilder;
};
} // namespace detent::local::detail
