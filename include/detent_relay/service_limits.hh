#pragma once
#include <cstddef>

namespace detent::relay {
// Admission lanes: requests in flight per route kind (history routes ride the opens lane).
struct ServiceLimits {
	std::size_t opens = 16, polls = 640, closes = 16;
};
} // namespace detent::relay
