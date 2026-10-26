#pragma once
#include "borrowed_definition.hh"
#include "paged_graph_validator.hh"
#include "paged_page_access.hh"

namespace detent::local::detail {
struct PagedReadState {
	paged::Root root;
	std::span<const SegmentInput> segments;
	std::shared_ptr<const PagedPageAccess> access;
	std::optional<BorrowedDefinition> definition(std::string_view name_space, std::string_view key) const;
	std::optional<std::span<const char>> lookup(std::string_view typed_key, paged::PageKind) const;
};
} // namespace detent::local::detail
