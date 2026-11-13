#pragma once
#include "detent/cursor.hh"
#include "detent_local/derived_ids.hh"
#include <completion/api.h>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The agent store (docs/design/namespace-delivery.md D8, W9, W11; docs/status/namespace-s7.md): one
// directory holding `selected` (the namespace table), `change`, `owner.lock`, and every namespace's
// generation and segment files under names derived from what they hold.
namespace detent::local {
using distribution::NamespaceCursor;
using distribution::DirectoryCursor;
using DirectoryPosition = DirectoryCursor; // agent_store.cc's spelling until namespace-s6-cutover
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

// One namespace generation ready in a publication cycle: its files' bytes as the build wrote them.
struct PreparedGeneration {
	struct Segment {
		SegmentId id{};
		std::string pack, owner;
	};
	std::string name;
	NamespaceCursor cursor;
	GenerationId base{};
	std::uint32_t ordinal = 0;
	std::string image, read, live;
	// The segments this build wrote. The `.live` rows name these and the base's, which the table reaches.
	std::vector<Segment> segments;
};
// A published name the durable table reaches holds other bytes: D10's repair (a build at the next
// ordinal) applies to `name`'s namespace.
class ReachedNameMismatch : public std::runtime_error {
  public:
	ReachedNameMismatch(std::string name, std::string space)
		: std::runtime_error("agent store: a name the table reaches differs: " + name), name(std::move(name)),
		  space(std::move(space)) {}
	std::string name, space;
};
class StoreRefused : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};

// Qualification hook: runs after every primitive of startup and publication, naming it. Install before
// opening a store, never concurrently with one.
struct AgentStoreTesting {
	static void install(void (*)(void *, const char *) noexcept, void *) noexcept;
};

// The store's one writer. It holds `owner.lock` exclusively for its lifetime.
class AgentStore {
  public:
	struct Options {
		distribution::Profile profile;
		unsigned mode = 0644; // the PosixIO's artifact mode; `change` takes it too
		std::uint64_t maximum_segments = 1u << 20;
	};
	// Takes `owner.lock` (creating it and `change` in a new store), then D11's startup: one barrier, the
	// `.tmp-` names removed, the table read and each entry's `.image`, `.read` and `.live` checked against
	// it, and every `change` word bumped. A table that does not decode refuses the store; an entry whose
	// files do not verify fails that namespace alone.
	AgentStore(std::shared_ptr<kronuz::io::completion::PosixIO>, Options);
	~AgentStore();
	AgentStore(const AgentStore &) = delete;
	AgentStore &operator=(const AgentStore &) = delete;
	const StoreTable &table() const noexcept;
	// Namespaces whose entry did not verify at startup and has not been republished since.
	std::span<const std::string> failed() const noexcept;
	std::uint64_t change_word(std::size_t index) const noexcept;
	// One cycle (D8, D11): every file staged under `.tmp-` and linked to its derived name; owner records
	// and reused names synced as one group, payloads as another; the table written under `.tmp-`,
	// fsynced, renamed over `selected` and the directory synced; then `change`. A reused name is kept
	// when its bytes verify, and unlinked and relinked when they do not and the table does not reach it.
	// Returns the new table's sequence. Throws ReachedNameMismatch, leaving the table as it was.
	std::uint64_t publish(const DirectoryCursor &, std::span<const PreparedGeneration>);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::local
