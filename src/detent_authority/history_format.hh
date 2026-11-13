#pragma once
// History's files (namespace-delivery D6 "Files"): blocks, bases, seal files and
// the `layout` table. Every encoding is canonical, so every voter writes the same
// bytes; docs/status/namespace-s5.md describes them.
#include "state.hh"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace detent::authority::history {
// One key's last change in a range: its canonical definition bytes, or a tombstone.
struct Change {
	std::string key;
	Revision revision = 0;
	std::optional<std::string> definition;
	// The canonical size: a live record's equals its live-size footprint.
	std::uint64_t size() const noexcept { return key.size() + 16 + (definition ? definition->size() : 0); }
	bool operator==(const Change &) const = default;
};
using Changes = std::vector<Change>; // Sorted by key, one per key.
std::uint64_t bytes(const Changes &changes) noexcept;
// Each key's younger change wins; a base merge also drops tombstones.
Changes merge(const Changes &older, const Changes &younger, bool drop_tombstones = false);
// The changes whose revision is above `after`.
Changes trim(const Changes &changes, Revision after);

Digest sha256(std::string_view bytes);
std::string hex(const Identity &identity);
std::string block_name(std::string_view name_space, const Identity &incarnation, Revision first, Revision last);
std::string base_name(std::string_view name_space, const Identity &incarnation, Revision position);
std::string seal_name(std::uint64_t index);
inline constexpr std::string_view table_name = "layout";

// A block [first, last] of one namespace incarnation, or its base at `last`
// (first == 0); a level-0 section is a block stored inside its seal file.
struct Block {
	std::string name_space;
	Identity incarnation{};
	Revision first = 0, last = 0;
	Changes changes;
	bool operator==(const Block &) const = default;
};
std::string encode_block(const Block &block);
std::string encode_base(const Block &base);
Block decode_block(std::string_view bytes, const Limits &limits);
Block decode_base(std::string_view bytes, const Limits &limits);

// What one seal records for a namespace it changed: its directory entry at the
// seal and, when keys changed, its level-0 section. Seal 0 takes first bases.
struct SealNamespace {
	std::string name;
	Identity incarnation{};
	std::uint64_t directory = 0;
	Revision origin = 0, position = 0;
	std::uint64_t live = 0; // |st| at the seal.
	bool dropped = false, first_base = false;
	std::optional<std::string> section; // An encoded block.
	bool operator==(const SealNamespace &) const = default;
};
// A file a roll-up wrote: a block, or a base (first == 0, last its position).
struct Output {
	std::string name_space;
	Identity incarnation{};
	Revision first = 0, last = 0;
	std::uint64_t bytes = 0;
	Digest digest{};
	bool base = false;
	std::string name() const;
	bool operator==(const Output &) const = default;
};
struct SealFile {
	std::uint64_t index = 0, ordinal = 0, previous = 0;
	LayoutPolicy policy{};
	std::vector<SealNamespace> namespaces; // By name.
	std::vector<std::string> carried;	   // Encoded blocks written back as sections.
	std::vector<Output> outputs;
	bool operator==(const SealFile &) const = default;
};
// A seal file ends with the SHA-256 of what precedes it, so a chain verifies alone.
std::string encode(const SealFile &seal);
SealFile decode_seal(std::string_view bytes, const Limits &limits);

// The layout at a seal: per namespace its base, blocks and level-0 sections.
struct TableBlock {
	Revision first = 0, last = 0;
	std::uint64_t bytes = 0;
	Digest digest{};
	std::uint64_t seal = 0; // A level-0 section: the seal file holding it.
	bool carried = false;
	bool operator==(const TableBlock &) const = default;
};
struct TableNamespace {
	std::string name;
	Identity incarnation{};
	std::uint64_t directory = 0;
	Revision origin = 0, position = 0; // position: s, the namespace at the latest seal.
	std::uint64_t live = 0;
	std::uint64_t carries = 0;	// Periodic roll-ups carried in a row.
	bool touched = false;		// Changed since its last roll-up or carry.
	TableBlock base;			// first == 0; last is p. An empty base has no file.
	std::vector<TableBlock> blocks, level0; // Oldest first.
	bool operator==(const TableNamespace &) const = default;
};
struct Table {
	std::uint64_t index = 0, ordinal = 0;
	LayoutPolicy policy{};
	std::vector<TableNamespace> namespaces; // By name.
	bool operator==(const Table &) const = default;
	TableNamespace *find(std::string_view name);
	const TableNamespace *find(std::string_view name) const;
};
std::string encode(const Table &table);
Table decode_table(std::string_view bytes, const Limits &limits);
} // namespace detent::authority::history
