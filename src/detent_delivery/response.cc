#include "detent_delivery/response.hh"
#include "detent_distribution/encoded_update.hh"
#include "detent_distribution/memory_resource.hh"
#include <algorithm>
#include <limits>
#include <charconv>
#include <cstring>
#include <exception>
#include <http_parser.h>
#include <map>
#include <sha256.h>
namespace detent::delivery {
namespace d = distribution;
std::uint64_t maximum_response_wire_bytes(std::uint64_t logical_bytes) {
	const auto maximum = std::uint64_t(d::encoded_update_fragment_limit) * d::fragment_bytes;
	if (!logical_bytes || logical_bytes > maximum)
		throw std::invalid_argument("delivery payload exceeds fragment directory limit");
	const auto frames = logical_bytes / d::fragment_bytes + 1;
	const auto frame = d::OwnedFragmentEncoder::maximum_wire_bytes();
	const auto half = (std::numeric_limits<std::uint64_t>::max() - 65536) / 2;
	if (!frame || frames > half / frame)
		throw std::length_error("delivery HTTP wire bound overflow");
	return frames * frame * 2 + 65536;
}
} // namespace detent::delivery
