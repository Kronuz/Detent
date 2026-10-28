#pragma once
#include <msgpack/bounded.hpp>

namespace detent::distribution {
using WireBytes = msgpack::bounded::InlineBytes<200>;
using WireToken = msgpack::bounded::BasicToken<WireBytes>;
using WireDecoder = msgpack::bounded::BasicDecoder<WireBytes>;
} // namespace detent::distribution
