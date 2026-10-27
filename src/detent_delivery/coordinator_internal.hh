#pragma once
#include "detent_delivery/coordinator.hh"
#include <atomic>
#include <future>
namespace detent::delivery::detail {
// Private fault seam, unavailable through installed/public interfaces.
struct CoordinatorAccess {
	struct PreparedProbePause {
		std::atomic<bool> reached{false};
		std::shared_future<void> resume;
	};
	static void pause_next_prepared_probe(Coordinator &, std::shared_ptr<PreparedProbePause>);
	struct ReconnectObservation {
		std::uint64_t count = 0;
		std::chrono::milliseconds delay{0};
	};
	static ReconnectObservation reconnect_observation(const Coordinator &);
	static bool retains_paged_candidate(const Coordinator &);
	static bool cancelled_construction_settled(const Coordinator &);
	static void request_compaction(Coordinator &);
	static void fail_next_compaction_admission(Coordinator &);
	static void fail_next_maintenance_recovery(Coordinator &, std::exception_ptr);
	static std::uint64_t completed_compactions(const Coordinator &);
	static void fail_next_worker_wait(Coordinator &);
	static void fail_worker_and_wait_after_ready(Coordinator &, std::exception_ptr);
	static void close_after_next_session_open(Coordinator &);
};
} // namespace detent::delivery::detail
