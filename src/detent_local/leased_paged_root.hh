#pragma once
#include "paged_root_prefix.hh"
#include "detent_local/generation.hh"
#include "paged_state_equivalence.hh"
#include "segment_registry.hh"
namespace detent::local::detail {
// Recovery alone seals this capability after checking actual named dependencies.
class LeasedPagedRoot final : public distribution::RetirementNode {
  public:
	LeasedPagedRoot(std::shared_ptr<const void> directory, std::shared_ptr<const void> lease,
					distribution::RetirementHandle<SegmentRegistry> registry, Descriptor descriptor,
					DirectoryIdentity identity, StorageIdentity storage, paged::Root root)
		: directory_(std::move(directory)), lease_(std::move(lease)), registry_(std::move(registry)),
		  descriptor_(descriptor), identity_(identity), storage_(storage), root_(root) {}
	const paged::Root &root() const {
		if (!sealed_)
			throw std::logic_error("unsealed leased paged root");
		return root_;
	}
	PagedStateCounts counts() const {
		root();
		return counts_;
	}
	const Descriptor &descriptor() const {
		root();
		return descriptor_;
	}
	DirectoryIdentity directory_identity() const {
		root();
		return identity_;
	}
	StorageIdentity storage_identity() const {
		root();
		return storage_;
	}
	const distribution::RetirementHandle<SegmentRegistry> &registry() const {
		root();
		return registry_;
	}

  protected:
	bool retire_step() noexcept override {
		if (registry_) {
			registry_.reset();
			return false;
		}
		if (lease_) {
			lease_.reset();
			return false;
		}
		if (directory_) {
			directory_.reset();
			return false;
		}
		return true;
	}

  private:
	void seal(PagedStateCounts counts) {
		if (sealed_ || !directory_ || !lease_ || !registry_ ||
			paged::encode_root(registry_->validated_root(), paged_format_limits()) != paged::encode_root(root_, paged_format_limits()) ||
			descriptor_.cursor != root_.cursor || descriptor_.profile != root_.profile ||
			std::uint64_t(counts.bools) + counts.ints != root_.records)
			throw std::invalid_argument("invalid leased paged root");
		counts_ = counts;
		sealed_ = true;
	}
	void seal_progress(const LeasedPagedRoot &base) {
		const auto old = base.root();
		if (sealed_ || !directory_ || !lease_ || registry_.get() != base.registry_.get() ||
			identity_ != base.identity_ || storage_ != base.storage_ || !reuses_paged_state(old, root_) ||
			descriptor_.cursor != root_.cursor || descriptor_.profile != root_.profile)
			throw std::invalid_argument("invalid leased paged progress root");
		counts_ = base.counts();
		sealed_ = true;
	}

	std::shared_ptr<const void> directory_, lease_;
	distribution::RetirementHandle<SegmentRegistry> registry_;
	Descriptor descriptor_;
	DirectoryIdentity identity_;
	StorageIdentity storage_;
	paged::Root root_;
	PagedStateCounts counts_;
	bool sealed_ = false;
	friend class RecoveryEngine;
	friend RecoveredGeneration detent::local::recover_generation(const std::filesystem::path &, ViewLimits,
																 std::optional<distribution::Cursor>);
	friend RecoveredGeneration detent::local::recover_generation(const std::filesystem::path &, ViewLimits,
																 std::optional<distribution::Cursor>,
																 const RecoveredGeneration *);
};
} // namespace detent::local::detail
