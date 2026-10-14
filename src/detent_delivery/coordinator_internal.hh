#pragma once
#include "detent_delivery/coordinator.hh"
namespace detent::delivery::detail {
// Private fault seam, unavailable through installed/public interfaces.
struct CoordinatorAccess {
	static void enable_paged_writer(Coordinator &);
	static void request_compaction(Coordinator &);
	static std::uint64_t completed_compactions(const Coordinator &);
	static void fail_next_worker_wait(Coordinator &);
	static void fail_worker_and_wait_after_ready(Coordinator &, std::exception_ptr);
	static void close_after_next_session_open(Coordinator &);
};
} // namespace detent::delivery::detail
