#pragma once
#include "../detent_cli/daemon.hh"
#include "detent_delivery/coordinator.hh"
#include <asio/awaitable.hpp>
#include <chrono>

namespace detent_agent::detail {
bool may_announce_ready(const detent::delivery::CoordinatorStatus &, bool require_prepared) noexcept;
asio::awaitable<void> monitor(std::shared_ptr<detent::delivery::Coordinator>, detent_cli::Ready,
                             bool require_prepared, std::chrono::milliseconds preparation_timeout);
}
