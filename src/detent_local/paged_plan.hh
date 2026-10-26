#pragma once
#include "detent_distribution/buffer.hh"
#include "paged_reuse.hh"
#include "paged_snapshot_builder.hh"
#include "state_equivalence_proof.hh"
#include <sha256.h>
#include <vector>
namespace detent::local::detail {
enum class PagedPlanIntent { Fresh, Forward, TrustedReset, Maintenance };
class PagedPlanState final : public distribution::RetirementNode {
  public:
	struct Forward {};
	struct PhysicalMaintenance {};
    struct Republish {};
	PagedPlanState(std::shared_ptr<distribution::StorageLedger>, std::optional<RecoveredGeneration>,
				   distribution::RetirementHandle<PagedImageOwner>, StorageIdentity, DirectoryIdentity,
				   PagedPlanIntent);
	PagedPlanState(std::shared_ptr<distribution::StorageLedger>,
				   distribution::RetirementHandle<LeasedPagedRoot>, GenerationId, PhysicalMaintenance);
	PagedPlanState(std::shared_ptr<distribution::StorageLedger>,
				   distribution::RetirementHandle<LeasedPagedRoot>, GenerationId,
				   const distribution::ValidatedDelta &);
	PagedPlanState(std::shared_ptr<distribution::StorageLedger>,
				   distribution::RetirementHandle<LeasedPagedRoot>, GenerationId,
				   const distribution::ValidatedDelta &, Forward);
    PagedPlanState(std::shared_ptr<distribution::StorageLedger>,
                   distribution::RetirementHandle<LeasedPagedRoot>, GenerationId, Republish);
	PagedPlanState(std::shared_ptr<distribution::StorageLedger>, RecoveredGeneration,
				   distribution::RetirementHandle<PagedImageOwner>, StorageIdentity);
	bool maintenance() const;
	PagedPlanIntent intent() const;
	std::optional<Descriptor> selected_base() const;
	const Descriptor &descriptor() const;
	const paged::Root &root() const;
	const Descriptor &base_descriptor() const;
	DirectoryIdentity directory_identity() const;
	StorageIdentity storage_identity() const;
	const distribution::RetirementHandle<SegmentRegistry> &registry() const;
	PagedStateCounts counts() const;
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const;
	std::span<const BuiltSegment> segments() const;
	SegmentReachability base_reachability(std::size_t) const;
	SegmentReachability new_reachability(std::size_t) const;
	std::span<const std::uint32_t, 257> override_histogram() const;

  protected:
	bool retire_step() noexcept override;

  private:
	distribution::RetirementHandle<LeasedPagedRoot> base_;
	std::optional<RecoveredGeneration> construction_base_;
	distribution::RetirementHandle<PagedImageOwner> replacement_;
	StorageIdentity replacement_storage_{};
	DirectoryIdentity construction_directory_{};
	PagedPlanIntent intent_ = PagedPlanIntent::Forward;
	std::optional<StateEquivalenceProof> maintenance_proof_;
	distribution::RetirementHandle<SegmentRegistry> registry_;
	paged::Root root_;
	Descriptor descriptor_;
	PagedStateCounts counts_;
	std::shared_ptr<distribution::OwnedBuffer> bytes_;
	std::optional<PagedImage> image_;
	std::vector<SegmentReachability, distribution::StorageAllocator<SegmentReachability>> base_live_,
		new_live_;
	std::array<std::uint32_t, 257> histogram_{};
	std::size_t image_capacity_ = 0;
	std::array<char, paged::root_bytes> encoded_{};
	std::size_t copied_ = 0, hashed_ = 0;
	class SHA256 hash_;
	bool republish_ = false;
	bool sealed_ = false;
    void copy_root(std::shared_ptr<distribution::StorageLedger>, distribution::Cursor);
	friend class PagedPlanBuilder;
	friend class PagedDeltaPlanBuilder;
	friend class SparsePlanFinalizer;
	friend class PagedMaintenanceBuilder;
	friend class PagedSnapshotPlanBuilder;
	friend class PagedCompactionBuilder;
};
// Move-only exact-base publication authority; retirement never runs on its caller.
class PagedPlan {
  public:
	PagedPlan(PagedPlan &&) noexcept = default;
	PagedPlan(const PagedPlan &) = delete;
	const Descriptor &descriptor() const { return checked_state().descriptor(); }
	bool maintenance() const { return checked_state().maintenance(); }
	PagedPlanIntent intent() const { return checked_state().intent(); }
	std::optional<Descriptor> selected_base() const { return checked_state().selected_base(); }
	const paged::Root &root() const { return checked_state().root(); }
	const Descriptor &base_descriptor() const { return checked_state().base_descriptor(); }
	DirectoryIdentity directory_identity() const { return checked_state().directory_identity(); }
	StorageIdentity storage_identity() const { return checked_state().storage_identity(); }
	// The verified base registry supplies reused dependencies; new artifacts remain separate.
	const distribution::RetirementHandle<SegmentRegistry> &registry() const {
		return checked_state().registry();
	}
	PagedStateCounts counts() const { return checked_state().counts(); }
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const { return checked_state().bytes(); }
	std::span<const BuiltSegment> segments() const { return checked_state().segments(); }
	SegmentReachability base_reachability(std::size_t index) const {
		return checked_state().base_reachability(index);
	}
	SegmentReachability new_reachability(std::size_t index) const {
		return checked_state().new_reachability(index);
	}
	std::span<const std::uint32_t, 257> override_histogram() const {
		return checked_state().override_histogram();
	}

  private:
	const PagedPlanState &checked_state() const {
		if (!state_)
			throw std::logic_error("moved paged publication plan");
		return *state_;
	}
	explicit PagedPlan(distribution::RetirementHandle<PagedPlanState> state) : state_(std::move(state)) {}
	distribution::RetirementHandle<PagedPlanState> state_;
	friend class PagedPlanBuilder;
	friend class PagedDeltaPlanBuilder;
	friend class PagedMaintenanceBuilder;
	friend class PagedSnapshotPlanBuilder;
	friend class PagedCompactionBuilder;
};
} // namespace detent::local::detail
