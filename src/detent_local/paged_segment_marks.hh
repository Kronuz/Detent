#pragma once
#include "paged_certificate.hh"
#include <optional>
#include <vector>

namespace detent::local::detail {
// A complete surviving-root census supplies all certificates before seal.
class PagedSegmentMarks final : public distribution::RetirementNode {
  public:
	PagedSegmentMarks(std::shared_ptr<distribution::StorageLedger>, DirectoryIdentity, StorageIdentity,
					  std::size_t capacity);
	void begin(distribution::RetirementHandle<PagedDependencyCertificate>);
	void step(ValidationBudget &);
	bool busy() const noexcept;
	void seal();
	void query(StorageIdentity);
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
	StorageIdentity storage_, sought_{};
	DirectoryIdentity directory_;
	std::size_t capacity_, size_ = 0, row_ = 0, slot_ = 0, probes_ = 0, charge_ = 0;
	bool positioned_ = false, sealed_ = false, failed_ = false, querying_ = false;
	static std::size_t hash(const StorageIdentity &) noexcept;
	bool admit(ValidationBudget &);
};
} // namespace detent::local::detail
