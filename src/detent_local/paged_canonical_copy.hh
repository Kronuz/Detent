#pragma once
#include "paged_graph_validator.hh"
#include "paged_snapshot_builder.hh"
#include <vector>

namespace detent::local::detail {
// A generation relocated into seal order (dense-distribution D7.4): the full build's pages, order
// and offsets in segments derived from `generation` from ordinal 0, so a full build under the same
// generation id writes the same files.
struct CanonicalCopy {
	paged::Root root;
	std::vector<BuiltSegment> segments;
};
// `inputs` hold every page `root` reaches, sorted as find_segment searches them, and are a validated
// (canonical, dense-pages D9) generation. The copy reads page headers and rewrites placement only:
// it computes no page digest (dense-pages D8), and hashes only the new packs and their ids.
CanonicalCopy canonical_copy(std::shared_ptr<distribution::StorageLedger>,
							 std::span<const SegmentInput> inputs, const paged::Root &root,
							 const GenerationId &generation, paged::Limits = {});
} // namespace detent::local::detail
