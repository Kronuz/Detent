#pragma once
#include "detent/cursor.hh"
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The layout a relay adopts (docs/status/namespace-s8.md): per namespace, the upstream's files and
// the positions each covers. One encoding serves the mirror's durable table and the layouts an
// upstream (the authority or a relay) answers over HTTP. No I/O: the authority links it to export.
namespace detent::relay {
using Digest = std::array<char, 32>;
inline constexpr std::uint32_t mirror_layout = 1;

// One immutable file of the upstream's layout. It covers positions [first, last] of its namespace:
// a base holds the state at `last` (`first` is 0), a block or section the changes in [first, last],
// a full copy the state at `last`.
struct MirrorFile {
	enum class Kind : std::uint8_t { Base = 1, Block = 2, Section = 3, Copy = 4 };
	Kind kind = Kind::Base;
	std::string name; // the upstream's name: 1 to 255 bytes, no NUL
	std::uint64_t first = 0, last = 0, size = 0;
	Digest digest{};
	bool operator==(const MirrorFile &) const = default;
};
// One namespace's layout at an upstream seal: the base at the cut p (no file when the namespace was
// empty at p), the blocks above level 0 and the level-0 sections, oldest first and contiguous from
// p + 1 to s, and at most one full copy at s.
struct NamespaceLayout {
	std::string name;
	distribution::Identity incarnation{};
	std::uint64_t seal = 0, ordinal = 0; // the upstream seal this layout is at, and its seal ordinal
	std::uint64_t cut = 0;               // p
	std::string policy;                  // the policy in force at that seal, as the upstream encodes it
	std::vector<MirrorFile> files;       // the base, blocks, sections, then the copy
	std::uint64_t end() const noexcept;  // s
	bool operator==(const NamespaceLayout &) const = default;
};
// `mirror`: replaced atomically, read whole. Entries are sorted by name; no file name repeats.
struct MirrorTable {
	std::uint64_t sequence = 0;
	std::vector<NamespaceLayout> entries;
	const NamespaceLayout *find(std::string_view name) const noexcept;
	bool operator==(const MirrorTable &) const = default;
};
class MirrorRefused : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
std::string encode_mirror_table(const MirrorTable &);
// Throws MirrorRefused for anything but a whole, current table.
MirrorTable decode_mirror_table(std::span<const char>);
// Throws std::invalid_argument when the files are not an optional base at the cut, blocks, sections
// and at most one copy, contiguous from the cut to s, or a name is invalid.
void check_layout(const NamespaceLayout &);
// The file name the mirror stores an upstream name under: `m-` and 32 hex digits of its SHA-256, so
// any upstream name, a namespace with `/` included, is one flat entry.
std::string mirror_file_name(std::string_view upstream_name);
// The SHA-256 of `bytes`, as a layout names a file's digest.
Digest digest_of(std::string_view bytes);
} // namespace detent::relay
