#pragma once
#include "detent_delivery/client.hh"
#include <string_view>

namespace detent::delivery::detail {
// The one mapping from a session control response (status, `error`, optional `reason`) to why the
// session reset; std::invalid_argument for a response no session endpoint sends.
SessionResetReason session_reset_reason(unsigned status, std::string_view error, std::string_view reason);
// The transport half of ClientOptions: TLS that verifies its peer, endpoint, host, token, bounds.
void validate_transport(const ClientOptions &);
} // namespace detent::delivery::detail
