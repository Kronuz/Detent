#pragma once
#include "allocator.hh"
#include <allocators/retirement.h>

namespace detent::distribution {
using allocators::RetirementDomain;
using allocators::RetirementDrain;
using allocators::RetirementFactory;
using allocators::RetirementNode;
using allocators::RetirementRuntime;
template <class T> using RetirementHandle = allocators::RetirementHandle<T>;
namespace detail {
using allocators::detail::RetirementPoint;
using allocators::detail::RetirementTesting;
} // namespace detail
} // namespace detent::distribution
