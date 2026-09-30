#pragma once

// Internal hook shared by global.cc and testing.cc: the thread-local
// override that TestGuard installs. Not part of the public API surface.

#include "detent/client.hh"

namespace detent::detail {

[[nodiscard]] Client*& test_client_slot() noexcept;

} // namespace detent::detail
