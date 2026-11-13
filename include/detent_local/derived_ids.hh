#pragma once
#include "detent/cursor.hh"
#include "detent_local/generation.hh"
#include "detent_local/ownership.hh"
#include <array>
#include <cstdint>
#include <optional>

// Every generation and segment id (docs/design/namespace-delivery.md D8.1, D8.2), derived from what
// was built and from no storage identity, so two builds of one input on any host name the same files.
namespace detent::local {
inline constexpr std::uint32_t agent_store_layout = 1;
// SHA-256 over a domain, the layout, the profile, the incarnation, the position, the base generation
// (zero for a build from a full copy) and the build ordinal, truncated to 16 bytes.
GenerationId derive_generation(const distribution::Profile &, const distribution::Identity &incarnation,
							   std::uint64_t position, const GenerationId &base, std::uint32_t ordinal);
// A generation's segment `ordinal`.
SegmentId derive_segment(const GenerationId &generation, std::uint64_t ordinal);
// D88: the old cursor as a namespace cursor until namespace-s6-cutover. The incarnation digests the
// group, restore epoch and scope; the position is the revision.
distribution::NamespaceCursor namespace_cursor(const distribution::Cursor &);
// The build ordinal over `selected`: r + 1 for a build at its unchanged head (r its ordinal), else 0.
std::uint32_t build_ordinal(const std::optional<Descriptor> &selected, const distribution::Cursor &target);
struct DerivedGeneration {
	GenerationId id{};
	std::uint32_t ordinal = 0;
	bool operator==(const DerivedGeneration &) const = default;
};
// The name a build at `target` over `selected` publishes under. A build from a full copy has a zero
// base; any other build's base is `selected`.
DerivedGeneration next_generation(const std::optional<Descriptor> &selected, const distribution::Cursor &target,
								  const distribution::Profile &, bool full_copy);
} // namespace detent::local
