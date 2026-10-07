#pragma once
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
	std::shared_ptr<const std::string> bytes() const;
	Descriptor descriptor() const;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::local
