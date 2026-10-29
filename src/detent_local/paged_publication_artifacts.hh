#pragma once
#include "detent_local/disk_ledger.hh"
#include "mapped_recovery_admission.hh"
#include "paged_plan.hh"
namespace detent::local::detail {
struct PublicationArtifact {
	std::array<char, 64> name{};
	std::size_t name_size = 0, record_size = 0;
	std::array<char, descriptor_bytes> record{};
	std::shared_ptr<const distribution::OwnedBuffer> payload;
	DiskUsage prefix;
	std::string_view filename() const noexcept { return {name.data(), name_size}; }
	std::string_view bytes() const noexcept {
		return payload ? payload->view() : std::string_view(record.data(), record_size);
	}
};
struct ReusedDependency {
	SegmentOwnership ownership;
	SegmentFileIdentity identity;
};
// Bind the retirement owner before populating variable admitted artifact rows.
class PagedPublicationArtifacts final : public distribution::RetirementNode {
  public:
	explicit PagedPublicationArtifacts(std::shared_ptr<distribution::StorageLedger>);
	void populate(const PagedPlan &, ReaderMetadataEncoding = ReaderMetadataEncoding::Legacy);
	void attach(PagedPlan &&);
	void attach_recovery(distribution::RetirementHandle<MappedRecoveryAdmission>);
	distribution::RetirementHandle<MappedRecoveryAdmission> recovery_admission() const;
	std::span<const PublicationArtifact> artifacts() const;
	std::span<const ReusedDependency> reused() const;
	const PagedPlan &plan() const;
	DiskUsage usage() const;
	std::uint64_t target_physical_bytes() const;
	DiskUsage prefix_usage(std::size_t) const;

  protected:
	bool retire_step() noexcept override;

  private:
	using Allocator = distribution::StorageAllocator<PublicationArtifact>;
	std::vector<PublicationArtifact, Allocator> artifacts_;
	std::vector<ReusedDependency, distribution::StorageAllocator<ReusedDependency>> reused_;
	std::optional<PagedPlan> plan_;
	distribution::RetirementHandle<MappedRecoveryAdmission> recovery_;
	DiskUsage usage_;
	std::uint64_t physical_bytes_ = paged::root_bytes;
	Descriptor descriptor_;
	std::optional<Descriptor> base_descriptor_;
	DirectoryIdentity directory_;
	StorageIdentity storage_;
	bool populated_ = false, failed_ = false;
	void append(std::string_view, std::span<const char>, std::shared_ptr<const distribution::OwnedBuffer>,
				bool generation = false);
};
} // namespace detent::local::detail
