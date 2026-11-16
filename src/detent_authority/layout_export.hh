#pragma once
// What a relay reads from a voter (W8; docs/status/namespace-s8.md, "The service"): each namespace's
// layout at the latest seal, in the relay's layout encoding, and the files it names, byte for byte.
// The layout tiles positions exactly as AuthorityHistory answers W6 from the same seal.
#include "detent_relay/layout.hh"
#include "history_layout.hh"
#include "state.hh"
#include <map>
#include <optional>
#include <span>

namespace detent::authority {
// On the owner. A table holds each tier's digest but not its stored size, so the first export of a
// file reads it once; sizes are kept by name (names are immutable) while the table reaches them.
class LayoutExport {
  public:
	// The layout of each named live namespace, in name order; an absent or dropped one is omitted. A
	// live namespace the table does not hold at its incarnation (created or re-created since the
	// latest seal that wrote it) has no files and its cut at its origin. Nothing when there is no
	// sealed layout to export: positions not enabled, or the table not at `sealed`.
	std::optional<std::vector<relay::NamespaceLayout>> layouts(std::span<const std::string> names, const State &state,
															   const State &sealed, const history::Table &table,
															   history::Files &files);
	// The bytes of the file `name` of `name_space`'s layout in `table`, digest-checked; nothing when
	// the layout names no such file.
	static std::optional<std::string> file(std::string_view name_space, std::string_view name,
										   const history::Table &table, history::Files &files, const Limits &limits);

  private:
	std::map<std::string, std::uint64_t, std::less<>> sizes_;
	std::uint64_t pruned_at_ = 0;
};
} // namespace detent::authority
