#pragma once
#include "borrowed_definition.hh"
#include "paged_graph_validator.hh"

namespace detent::local::detail {
struct PagedReadState {
	paged::Root root;
	std::span<const SegmentInput> segments;
	std::optional<BorrowedDefinition> definition(std::string_view name_space, std::string_view key) const;
};
} // namespace detent::local::detail
