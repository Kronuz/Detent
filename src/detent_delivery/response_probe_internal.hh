#pragma once
#include "detent_delivery/response.hh"
#include <array>

namespace detent::delivery::detail {
// Transport observation only. Contains no validated state or publication proof.
struct ProbeOffer {
	std::uint64_t window;
	distribution::Cursor target;
	distribution::UpdateKind kind;
	std::uint64_t logical_bytes, wire_bytes;
	std::array<unsigned char, 32> digest;
};
struct ProbeDecoderAccess {
	static void prepare(ResponseDecoder &, bool reference = false);
	static ProbeOffer take(ResponseDecoder &);
};
} // namespace detent::delivery::detail
