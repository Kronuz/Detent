#pragma once
#include "detent_local/view.hh"

namespace detent::local {
// Private validated storage retained by SDK views and worker-side export.
struct ReadView::Root : distribution::RetirementNode {
	std::shared_ptr<const void> lease;
	std::span<const char> bytes;
	Header header;
	std::uint32_t bools = 0, ints = 0;
	Root(std::shared_ptr<const void>, std::span<const char>, Header);
	std::uint64_t number(std::uint64_t offset, unsigned width) const;
	std::string_view text(std::uint64_t row) const;
	std::optional<std::uint64_t> find(std::string_view name_space, std::string_view key) const;
};
} // namespace detent::local
