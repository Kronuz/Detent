#pragma once
#include "detent_delivery/coordinator.hh"
namespace detent::delivery::detail {
// Private fault seam, unavailable through installed/public interfaces.
struct CoordinatorAccess {
	static void fail_next_worker_wait(Coordinator &);
};
} // namespace detent::delivery::detail
