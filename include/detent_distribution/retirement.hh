#pragma once
#include "allocator.hh"
#include <allocators/retirement.h>
namespace detent::distribution {
using allocators::RetirementNode;
using allocators::RetirementDomain;
using allocators::RetirementFactory;
using allocators::RetirementRuntime;
using allocators::RetirementDrain;
template <class T> using RetirementHandle = allocators::RetirementHandle<T>;
namespace detail { using allocators::detail::RetirementTesting; }
} // namespace detent::distribution
