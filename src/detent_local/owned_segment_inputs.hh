#pragma once
#include "paged_dependency_walk.hh"
namespace detent::local::detail {
// Owns identity-sorted immutable buffers for checked traversal and validation.
class OwnedSegmentInputs {
  public:
	OwnedSegmentInputs(distribution::RetirementFactory, std::shared_ptr<distribution::StorageLedger>,
					   StorageIdentity storage, std::size_t segments, std::uint64_t bytes);
	~OwnedSegmentInputs();
	OwnedSegmentInputs(OwnedSegmentInputs &&) noexcept;
	OwnedSegmentInputs(const OwnedSegmentInputs &) = delete;
	void begin(SegmentOwnership, std::shared_ptr<const distribution::OwnedBuffer>);
	void step(ValidationBudget &);
	bool busy() const noexcept;
	void freeze();
	std::span<const SegmentInput> inputs() const;
	std::shared_ptr<const void> owner() const;
	static std::optional<SegmentInput> lookup(const void *, const StorageIdentity &);

  private:
	struct State;
	distribution::RetirementHandle<State> state_;
	std::shared_ptr<distribution::RetirementHandle<State>> pin_;
};
} // namespace detent::local::detail
