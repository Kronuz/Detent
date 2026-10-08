#pragma once
#include "generation.hh"
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

  private:
	kronuz::io::completion::AllocationContext context_;
	std::shared_ptr<kronuz::io::completion::IO> io_;
	std::shared_ptr<void> admission_;
	std::unique_ptr<kronuz::io::completion::OwnerLock> lock_;
	DirectoryIdentity directory_;
	kronuz::io::completion::Identity incarnation_{};
	std::uint64_t operation_ = 0;
};
std::shared_ptr<StorageSession> make_storage_session(kronuz::io::completion::AllocationContext context,
													 std::shared_ptr<kronuz::io::completion::IO> io,
													 std::shared_ptr<void> admission, bool create_owner);
} // namespace detent::local
