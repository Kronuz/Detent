#pragma once
#include "allocator.hh"
#include <allocators/buffer.h>

namespace detent::distribution {
using allocators::make_owned_buffer;
using allocators::OwnedBuffer;
} // namespace detent::distribution
