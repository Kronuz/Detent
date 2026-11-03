#pragma once
#include "detent_delivery/client.hh"
#include "response_probe_internal.hh"

namespace detent::delivery::detail {
using ProbeEvent = std::variant<ProbeOffer, SessionIdle, SessionReset>;
// Feedback records transport consumption, never durable publication. Each handle
// binds to semantic, probe or reference Next operations for its entire lifetime.
struct ProbeAccess {
	static task::task<ProbeEvent> next(ClientOptions, SessionHandle &, std::optional<PublicationFeedback> = {},
									   std::chrono::milliseconds wait = std::chrono::seconds(30),
									   task::cancel_token = {});
	// Full schema validation and budgeted disposal precede the reference result.
	static task::task<ProbeEvent> reference(ClientOptions, SessionHandle &, std::optional<PublicationFeedback> = {},
											std::chrono::milliseconds wait = std::chrono::seconds(30),
											task::cancel_token = {});
};
// logical_bytes and digest cover concatenated decoded payload. wire_bytes
// counts frame headers and encoded payload, excluding HTTP/TLS overhead.
} // namespace detent::delivery::detail
