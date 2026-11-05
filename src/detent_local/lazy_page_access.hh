#pragma once
#include "detent_distribution/memory_resource.hh"
#include "detent_local/view.hh"
#include "paged_page_access.hh"
#include "detent_local/ownership.hh"
#include <optional>
#include <sys/types.h>

namespace detent::local::detail {
enum class LazyPageWindow { BeforePreparation, PreparationCounted, PreparationBlocked, ForkWaiting, Miss, Publish, Mark };
// Qualification hook: runs after a segment miss, before its publish and before a page's bit is set.
// Install before any lookup, never concurrently with one.
struct LazyPageTesting {
	static void install(void (*)(LazyPageWindow, void *) noexcept, void *) noexcept;
};
// Lock-free page resolution: one segment table per generation and a verified bit per page
// (docs/mechanisms/verified-page-bitmap.md). Plain ownership: destruction unmaps every segment.
class PageTable final : public PagedPageAccess {
  public:
	PageTable(int directory_fd, uid_t publisher, StorageIdentity, paged::Root, ViewLimits,
	          std::shared_ptr<distribution::StorageLedger> heap,
	          std::shared_ptr<distribution::StorageLedger> mappings);
	~PageTable();
	PageTable(PageTable &&) noexcept;
	std::span<const char> resolve(const paged::Reference &) const override;
	std::uint32_t maximum_overrides() const noexcept override;
	bool available() const noexcept override;
	std::size_t mapped_segments() const;
	std::uint64_t page_verifications() const noexcept;
	// Fork preparation runs with the owning handle refresh flags held.
	void prepare_fork() noexcept;
	void parent_after_fork() noexcept;
	void child_after_fork() noexcept;
	// Unmaps one mapped segment; false once none remain. Never concurrent with resolve.
	bool release_one() noexcept;

  private:
	struct State;
	distribution::StorageOwner<State> state_;
};
// A PageTable retired through a RetirementDomain, holding the directory and lease until it retires.
class LazyPageAccess final : public PagedPageAccess, public distribution::RetirementNode {
  public:
	LazyPageAccess(std::shared_ptr<const void> directory_and_lease, int directory_fd, uid_t publisher,
	               StorageIdentity, paged::Root, ViewLimits, std::shared_ptr<distribution::StorageLedger> heap,
	               std::shared_ptr<distribution::StorageLedger> mappings);
	~LazyPageAccess();
	std::span<const char> resolve(const paged::Reference &) const override;
	std::uint32_t maximum_overrides() const noexcept override;
	bool available() const noexcept override;
	std::size_t mapped_segments() const;
	std::uint64_t page_verifications() const noexcept;

  protected:
	bool retire_step() noexcept override;

  private:
	std::shared_ptr<const void> lease_;
	std::optional<PageTable> table_;
};
} // namespace detent::local::detail
