#pragma once
#include "detent_distribution/framing.hh"
#include <cstdint>
namespace detent::delivery {
enum class SessionResponsePurpose { Open, Next, Close };
// The HTTP bytes a response carrying `logical_bytes` of fragments may take on the wire.
std::uint64_t maximum_response_wire_bytes(std::uint64_t logical_bytes);
} // namespace detent::delivery
