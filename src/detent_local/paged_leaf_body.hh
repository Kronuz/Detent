#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_local/paged_definition.hh"
#include "detent_local/view.hh"
namespace detent::local::detail {
using OverrideAt = paged::Override (*)(const void *, std::uint64_t ordinal, std::uint32_t index);
// Callback text stays borrowed until encoding or worker-side disposal completes.
class LeafBody {
  public:
	LeafBody(std::shared_ptr<distribution::StorageLedger>, paged::Definition, const void *,
			 std::uint64_t ordinal, OverrideAt, std::size_t maximum_body);
	LeafBody(std::shared_ptr<distribution::StorageLedger>, std::uint64_t watermark);
	void step(ValidationBudget &);
	bool done() const noexcept;
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const;
	bool discard(ValidationBudget &);

  private:
	enum class Phase { Count, Zero, Header, Overrides, Done };
	bool grant(std::size_t, ValidationBudget &);
	std::shared_ptr<distribution::StorageLedger> account_;
	std::shared_ptr<distribution::OwnedBuffer> output_;
	paged::Definition definition_;
	const void *context_ = nullptr;
	OverrideAt override_ = nullptr;
	std::uint64_t ordinal_ = 0;
	std::size_t maximum_ = 0, size_ = 0, offset_ = 0, charge_ = 0;
	std::uint32_t index_ = 0;
	std::string_view previous_;
	Phase phase_ = Phase::Count;
	bool namespace_ = false, failed_ = false, discarding_ = false;
};
} // namespace detent::local::detail
