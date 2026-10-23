#pragma once
#include "detent_local/view.hh"
#include <stdexcept>

namespace detent::local::detail {
inline paged::Limits paged_limits(const ViewLimits &limits, std::uint64_t manifest_bytes = 0) {
	if (limits.bytes < manifest_bytes)
		throw std::length_error("paged manifest exceeds configured byte limit");
	paged::Limits result;
	result.bytes = limits.bytes - manifest_bytes;
	result.records = limits.records;
	result.namespaces = limits.namespaces;
	result.pages = limits.pages;
	result.segments = limits.segments;
	return result;
}
} // namespace detent::local::detail
