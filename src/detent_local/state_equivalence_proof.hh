#pragma once
#include "detent_local/generation.hh"
#include <utility>
namespace detent::local::detail {
class StateEquivalence;
// Semantic evidence binds physical heads without granting filesystem recovery authority.
class StateEquivalenceProof {
  public:
	StateEquivalenceProof(StateEquivalenceProof &&other) noexcept
		: source_(other.source_), target_(other.target_), source_directory_(other.source_directory_),
		  target_directory_(other.target_directory_), valid_(std::exchange(other.valid_, false)) {}
	StateEquivalenceProof(const StateEquivalenceProof &) = delete;
	bool binds(const RecoveredGeneration &source, const RecoveredGeneration &target) const noexcept {
		return valid_ && source.descriptor() == source_ && target.descriptor() == target_ &&
			   source_directory_ && source.directory_identity() == *source_directory_ &&
			   (!target_directory_ || target.directory_identity() == *target_directory_);
	}

  private:
	StateEquivalenceProof(Descriptor source, Descriptor target,
						  std::optional<DirectoryIdentity> source_directory,
						  std::optional<DirectoryIdentity> target_directory)
		: source_(source), target_(target), source_directory_(source_directory),
		  target_directory_(target_directory) {}
	bool binds_construction(const RecoveredGeneration &source, const Descriptor &target) const noexcept {
		return valid_ && source.descriptor() == source_ && target == target_ && source_directory_ &&
			   source.directory_identity() == *source_directory_ && !target_directory_;
	}
	Descriptor source_, target_;
	std::optional<DirectoryIdentity> source_directory_, target_directory_;
	bool valid_ = true;
	friend class StateEquivalence;
	friend class PagedMaintenanceBuilder;
};
} // namespace detent::local::detail
