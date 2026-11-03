#pragma once
#include "../detent_cli/daemon.hh"
#include "detent_delivery/coordinator.hh"
#include "task_asio.hh"
#include <chrono>

namespace detent_agent::detail {
bool may_announce_ready(const detent::delivery::CoordinatorStatus &, bool require_prepared) noexcept;
task::task<void> monitor(task::asio_scheduler &, std::shared_ptr<detent::delivery::Coordinator>, detent_cli::Ready,
						 bool require_prepared, std::chrono::milliseconds preparation_timeout);
}
