#pragma once
#include "detent_distribution/retirement.hh"
#include "generation.hh"
#include <atomic>
#include <completion/api.h>

namespace detent::local {
// Stable worker-side owner shared by publication, inventory and reclamation.
// Holds no operation or request; accepted operations may retain this session.
class StorageSession {
  public:
	StorageSession(kronuz::io::completion::AllocationContext context,
				   std::shared_ptr<kronuz::io::completion::IO> io, std::shared_ptr<void> admission,
				   bool create_owner);
	StorageSession(const StorageSession &) = delete;
	StorageSession &operator=(const StorageSession &) = delete;
	const std::shared_ptr<kronuz::io::completion::IO> &backend() const noexcept { return io_; }
	const kronuz::io::completion::AllocationContext &allocation_context() const noexcept { return context_; }
	DirectoryIdentity directory_identity() const noexcept { return directory_; }
	kronuz::io::completion::Token next_operation();
	// Revokes new identities; accepted originals retain their cleanup authority.
	void close() noexcept { closed_.store(true, std::memory_order_release); }
	bool closed() const noexcept { return closed_.load(std::memory_order_acquire); }

	class ManagedTag {
		ManagedTag() = default;
		friend std::shared_ptr<StorageSession>
		make_owned_storage_session(detent::distribution::RetirementDomain &, const std::filesystem::path &,
								   std::shared_ptr<void>, bool);
	};
	StorageSession(ManagedTag, kronuz::io::completion::AllocationContext context,
				   std::shared_ptr<kronuz::io::completion::IO> io);

  private:
	kronuz::io::completion::AllocationContext context_;
	std::shared_ptr<kronuz::io::completion::IO> io_;
	std::shared_ptr<void> admission_;
	std::unique_ptr<kronuz::io::completion::OwnerLock> lock_;
	DirectoryIdentity directory_;
	kronuz::io::completion::Identity incarnation_{};
	std::uint64_t operation_ = 0;
	std::atomic<bool> closed_{false};
	void initialize_identity();
};
std::shared_ptr<StorageSession> make_storage_session(kronuz::io::completion::AllocationContext context,
													 std::shared_ptr<kronuz::io::completion::IO> io,
													 std::shared_ptr<void> admission, bool create_owner);
// Startup worker only. The application keeps the retirement runtime alive
// through drainage. All exported backend references share the retired owner.
std::shared_ptr<StorageSession> make_owned_storage_session(detent::distribution::RetirementDomain &domain,
														   const std::filesystem::path &directory,
														   std::shared_ptr<void> admission,
														   bool create_owner);
} // namespace detent::local
