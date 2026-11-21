#pragma once
#include "borrowed_definition.hh"
#include "detent_local/view.hh"
#include "paged_read_state.hh"

namespace detent::local {
// Private validated storage retained by SDK views and worker-side export.
struct ReadView::Root : distribution::RetirementNode {
	std::shared_ptr<const void> lease;
	std::optional<detail::PagedReadState> paged;
	std::uint64_t overrides = 0, strings = 0;
	Root() = default;
	Root(std::shared_ptr<const void>, detail::PagedReadState, std::uint64_t overrides, std::uint64_t strings = 0);
	void install_paged(std::shared_ptr<const void>, detail::PagedReadState, std::uint64_t overrides,
					   std::uint64_t strings);
	std::optional<detail::BorrowedDefinition> definition(std::string_view name_space,
														 std::string_view key) const;
};
} // namespace detent::local
