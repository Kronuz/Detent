#pragma once
#include "delta_index.hh"
#include "paged_cursor.hh"
#include "paged_leaf_body.hh"
namespace detent::local::detail {
struct ChangedLeaf {
	std::string_view key;
	std::optional<std::span<const char>> body;
};
// Visits namespace watermarks and changed definitions; existing definitions stay untouched.
class DeltaLeaves {
  public:
	DeltaLeaves(const RecoveredGeneration &, std::shared_ptr<const distribution::ValidatedDelta>,
				paged::Limits = {});
	// A one-namespace generation's delta (a relay's build, dense distribution D7.2): the base holds
	// `name` alone, its watermark at the base's position.
	DeltaLeaves(std::string_view name, std::uint64_t watermark,
				std::shared_ptr<const distribution::ValidatedDelta>, paged::Limits = {});
	~DeltaLeaves();
	DeltaLeaves(DeltaLeaves &&) noexcept;
	DeltaLeaves(const DeltaLeaves &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	bool ready() const noexcept;
	ChangedLeaf leaf() const;
	void consume();
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
