#pragma once
#include "detent_distribution/allocator.hh"
#include <completion/asio_completion.h>
#include <completion/native_completion.h>

namespace detent::delivery {
using NativeDriver = kronuz::journal::AsioCompletionDriver<kronuz::io::completion::NativeQueue>;
struct CompletionSchedulerStatus {
	std::size_t participants = 0, queued = 0, peak_queued = 0;
	std::uint64_t grants = 0, maximum_wait_nanoseconds = 0;
	bool active = false;
};
class CompletionScheduler;
class CompletionPort {
  public:
	asio::awaitable<void> run_one(std::shared_ptr<kronuz::journal::IOOperation>);
	bool uses(const std::shared_ptr<NativeDriver> &) const noexcept;

  private:
	friend class CompletionScheduler;
	std::shared_ptr<CompletionScheduler> scheduler_;
	std::size_t index_ = 0;
};
// Fixed startup participants share one native original at a time on one owner.
class CompletionScheduler : public std::enable_shared_from_this<CompletionScheduler> {
  public:
	CompletionScheduler(asio::any_io_executor, std::shared_ptr<NativeDriver>,
						std::shared_ptr<distribution::StorageLedger>, std::size_t participants);
	~CompletionScheduler();
	CompletionPort port(std::size_t);
	bool idle() const noexcept;
	CompletionSchedulerStatus status() const noexcept;

  private:
	friend class CompletionPort;
	static asio::awaitable<void> run_owned(std::shared_ptr<CompletionScheduler>, std::size_t,
										   std::shared_ptr<kronuz::journal::IOOperation>);
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::delivery
