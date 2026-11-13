#pragma once
#include "paged_certificate.hh"
#include "live_record.hh"
#include <optional>
#include <vector>

namespace detent::local::detail {
// A complete surviving-root census supplies all certificates before seal.
class PagedSegmentMarks final : public distribution::RetirementNode {
  public:
	PagedSegmentMarks(std::shared_ptr<distribution::StorageLedger>, DirectoryIdentity, std::size_t capacity);
	void begin(distribution::RetirementHandle<PagedDependencyCertificate>);
	void begin_conservative(std::span<const LiveRow>);
	void step(ValidationBudget &);
	bool busy() const noexcept;
	void seal();
	void query(SegmentId);
	std::optional<bool> query_step(ValidationBudget &);
	std::size_t size() const noexcept { return size_; }

  protected:
	bool retire_step() noexcept override;

  private:
	struct Entry {
		SegmentOwnership ownership;
		bool occupied = false;
	};
	std::vector<Entry, distribution::StorageAllocator<Entry>> entries_;
	distribution::RetirementHandle<PagedDependencyCertificate> certificate_;
	std::span<const LiveRow> conservative_;
	bool conservative_active_ = false;
	SegmentId sought_{};
	DirectoryIdentity directory_;
	std::size_t capacity_, size_ = 0, row_ = 0, slot_ = 0, probes_ = 0, charge_ = 0;
	bool positioned_ = false, sealed_ = false, failed_ = false, querying_ = false;
	static std::size_t hash(const SegmentId &) noexcept;
	bool admit(ValidationBudget &);
};
} // namespace detent::local::detail
