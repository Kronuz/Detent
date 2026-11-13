#pragma once
// The layout of a group's history (namespace-delivery D6): each seal adds level-0
// sections, rolls namespaces up into blocks, squashes them by ratio and L, and
// cuts the oldest into the base by cost or the disk cap. Deterministic: the
// layout after a seal is a function of the layout before it and the seal file.
#include "history_format.hh"
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace detent::authority::history {
// The files a layout reaches, by name. A section is read from its seal file.
class Files {
  public:
	virtual ~Files() = default;
	virtual std::string read(std::string_view name) = 0; // Throws when absent.
};
// What one seal produces: its seal file, the block and base files its roll-ups
// wrote, and the table when the seal is a period seal.
struct Step {
	SealFile seal;
	std::string bytes; // The encoded seal file.
	std::vector<std::pair<std::string, std::string>> written;
	std::optional<Table> table;
};
enum class Tier : std::uint8_t { Ring = 1, Blocks, FullCopy };
// An answer for a store at `after`: the parts apply in order, then the ring's
// entries above `through`. A full copy has one part and no tombstones.
struct Answer {
	Tier tier = Tier::Ring;
	Revision through = 0;
	std::vector<std::string> names; // The files it reads, oldest first.
	std::vector<Changes> parts;
	std::uint64_t sent = 0, read = 0;
};
enum class Cut : std::uint8_t { Cost = 0, Cap = 1, Count = 2 };
class Layout {
  public:
	explicit Layout(Limits limits = {}) : limits_(limits) {}
	// Adopts a durable table, the layout at its seal.
	explicit Layout(Table table, Limits limits = {}) : limits_(limits), table_(std::move(table)) {}
	const Table &table() const noexcept { return table_; }
	bool empty() const noexcept { return !table_.index; }
	// The seal at `sealed`'s index, its sections cut from apply's accumulator.
	Step seal(const State &sealed, Files &files);
	// Recomputes a seal from its file; its carried sections and outputs must match.
	Step replay(std::string_view seal_file, Files &files);
	Answer answer(std::string_view name, const Identity &incarnation, Revision after, Revision ring_floor,
				  Files &files) const;
	// Cuts per namespace by the rule that made them (health; not replicated).
	const std::map<std::string, std::array<std::uint64_t, 3>> &cuts() const noexcept { return cuts_; }

  private:
	Step apply(SealFile input, Files &files, const std::map<std::string, Changes> &bases);
	Changes changes(const TableNamespace &n, const TableBlock &block, Files &files,
					const SealFile *current) const;
	Limits limits_;
	Table table_;
	std::map<std::string, std::array<std::uint64_t, 3>> cuts_;
};
// One tier of `n`: its base, a block or a level-0 section (read from its seal file, or from
// `current` when that is the seal holding it), checked against the table's digest and range.
Changes read_tier(const TableNamespace &n, const TableBlock &block, Files &files, const Limits &limits,
				  const SealFile *current = nullptr);
// L = ⌊|st| · α / 4⌋, exact in integers (D6 "Every input is replicated").
std::uint64_t block_limit(const LayoutPolicy &policy, std::uint64_t live) noexcept;
} // namespace detent::authority::history
