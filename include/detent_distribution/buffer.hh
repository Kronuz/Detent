#pragma once
#include "allocator.hh"
#include "storage.hh"
#include <allocators/buffer.h>
namespace detent::distribution {
using allocators::OwnedBuffer;
using allocators::make_owned_buffer;
} // namespace detent::distribution
