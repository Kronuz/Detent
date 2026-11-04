#pragma once
#include "detent_distribution/memory_resource.hh"
#include "detent_local/view.hh"
#include "paged_page_access.hh"
#include "detent_local/ownership.hh"
#include <sys/types.h>

namespace detent::local::detail {
enum class LazyPageWindow { Miss, Publish, Mark };
// Qualification hook: runs after a segment miss, before its publish and before a page's bit is set.
// Install before any lookup, never concurrently with one.
struct LazyPageTesting {
	static void install(void (*)(LazyPageWindow, void *) noexcept, void *) noexcept;
};
// Lock-free page resolution: one segment table per generation and a verified bit per page.
class LazyPageAccess final : public PagedPageAccess, public distribution::RetirementNode {
  public:
	LazyPageAccess(std::shared_ptr<const void> directory_and_lease, int directory_fd, uid_t publisher,
	               StorageIdentity, paged::Root, ViewLimits, distribution::RetirementFactory,
	               std::shared_ptr<distribution::StorageLedger> heap,
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
	struct State;
	distribution::StorageOwner<State> state_;
};
} // namespace detent::local::detail
