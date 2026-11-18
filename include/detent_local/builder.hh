#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_distribution/protocol.hh"
#include "derived_ids.hh"
#include "generation.hh"
namespace detent::local {
// Build-worker capability: borrows sealed semantic input through all work.
// Counting, zero filling, row construction and integrity hashing are resumable.
class ImageBuilder {
  public:
	// The image is named as derived_ids.hh derives it: a snapshot as a full copy, a delta over `base`.
	explicit ImageBuilder(std::shared_ptr<const distribution::ValidatedSnapshot> source, ViewLimits limits = {});
	ImageBuilder(std::shared_ptr<const distribution::ValidatedDelta> source, RecoveredGeneration base,
				 ViewLimits limits = {});
	~ImageBuilder();
	ImageBuilder(ImageBuilder &&) noexcept;
	ImageBuilder(const ImageBuilder &) = delete;
	void step(ValidationBudget &budget);
	bool done() const noexcept;
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const;
	Descriptor descriptor() const;
	std::optional<Descriptor> base_descriptor() const;
	std::optional<DirectoryIdentity> base_directory_identity() const;
	// Terminal worker-side disposal after cancellation or construction failure.
	// Keep validated input alive until this builder is destroyed, then drain the
	// input's own discard cursor separately.
	bool discard(ValidationBudget &budget);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local
