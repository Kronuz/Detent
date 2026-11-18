#pragma once
#include "detent/cursor.hh"
#include "detent_local/generation.hh"
#include "detent_local/ownership.hh"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Every generation and segment id (docs/design/namespace-delivery.md D8.1, D8.2), derived from what
// was built and from no storage identity, so two builds of one input on any host name the same files.
namespace detent::local {
inline constexpr std::uint32_t agent_store_layout = 1;
// SHA-256 over a domain, the layout, the profile, the incarnation, the position, the base generation
// (zero for a build from a full copy) and the build ordinal, truncated to 16 bytes.
GenerationId derive_generation(const distribution::Profile &, const distribution::Identity &incarnation,
							   std::uint64_t position, const GenerationId &base, std::uint32_t ordinal);
// A generation's segment `ordinal`; SHA-256 over segment_domain, the generation and the ordinal (8 bytes).
inline constexpr std::string_view segment_domain = "detent.agent-store.segment";
inline constexpr std::size_t segment_derivation_bytes = segment_domain.size() + sizeof(GenerationId) + 8;
SegmentId derive_segment(const GenerationId &generation, std::uint64_t ordinal);
// D88: the old cursor as a namespace cursor until namespace-s6-cutover. The incarnation digests the
// cluster, group, restore epoch and scope, so a reset changing any of them derives new names; the
// position is the revision.
distribution::NamespaceCursor namespace_cursor(const distribution::Cursor &);
// D88: the table entry of the old cursor's namespace, named after its scope (lowercase hex).
std::string namespace_name(const distribution::Cursor &);
// The build ordinal over `selected`: r + 1 for a build at its unchanged head (r its ordinal), else 0.
std::uint32_t build_ordinal(const std::optional<Descriptor> &selected, const distribution::Cursor &target);
// A generation's name: its id and the build ordinal its descriptor carries. A bare id names ordinal 0.
struct DerivedGeneration {
	DerivedGeneration(GenerationId id = {}, std::uint32_t ordinal = 0) : id(id), ordinal(ordinal) {}
	GenerationId id;
	std::uint32_t ordinal;
	bool operator==(const DerivedGeneration &) const = default;
};
// The name a build at `target` over `selected` publishes under. A build from a full copy has a zero
// base; any other build's base is `selected`.
DerivedGeneration next_generation(const std::optional<Descriptor> &selected, const distribution::Cursor &target,
								  const distribution::Profile &, bool full_copy);
} // namespace detent::local
