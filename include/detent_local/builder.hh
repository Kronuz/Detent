#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_distribution/protocol.hh"
#include "generation.hh"
namespace detent::local {
// Build-worker capability: borrows sealed semantic input through all work.
// Counting, zero filling, row construction and integrity hashing are resumable.
class ImageBuilder {
  public:
	ImageBuilder(std::shared_ptr<const distribution::ValidatedSnapshot> source, GenerationId generation,
				 ViewLimits limits = {});
	~ImageBuilder();
	ImageBuilder(ImageBuilder &&) noexcept;
	ImageBuilder(const ImageBuilder &) = delete;
	void step(ValidationBudget &budget);
	bool done() const noexcept;
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const;
	Descriptor descriptor() const;

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local
