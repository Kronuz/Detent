#pragma once
#include "detent/cursor.hh"
#include "detent_local/generation.hh"
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// `selected`, the agent store's namespace table (docs/status/namespace-s7.md): a pure codec that
// readers and the store share.
namespace detent::local {
using distribution::NamespaceCursor;
using distribution::DirectoryCursor;
struct TableEntry {
	std::string name;
	NamespaceCursor cursor;
	std::uint32_t slot = 0; // its sequence is word 1 + slot of `change`
	GenerationId generation{};
	std::uint32_t ordinal = 0; // build ordinal: 0, or r + 1 for a repair of ordinal r
	std::uint64_t root_length = 0;
	std::array<char, 32> root{}, read{}; // SHA-256 of `.image` and `.read`
	bool operator==(const TableEntry &) const = default;
};
// `selected`: replaced atomically, read whole. Entries are sorted by name.
struct StoreTable {
	std::uint64_t sequence = 0;
	DirectoryCursor directory;
	distribution::Profile profile;
	std::vector<TableEntry> entries;
	const TableEntry *find(std::string_view name) const noexcept;
	bool operator==(const StoreTable &) const = default;
};
class TableRefused : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
std::string encode_table(const StoreTable &);
// Throws TableRefused for anything but a whole, current table.
StoreTable decode_table(std::span<const char>);

// The bound on a table's encoding: its header, `entries` entries of the longest name, and its digest.
std::uint64_t table_bytes(std::uint64_t entries) noexcept;
} // namespace detent::local
