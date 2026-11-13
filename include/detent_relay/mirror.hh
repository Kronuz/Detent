#pragma once
#include "detent/cursor.hh"
#include <completion/api.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The relay's mirror (docs/design/namespace-delivery.md D7; docs/status/namespace-s8.md): per
// namespace, the upstream's base, blocks, level-0 sections and full copy, byte for byte, in one
// directory of its own. The mirror computes no layout and parses no file: it adopts the upstream's
// layout, fetches every file it lacks by name, verifies each digest, and publishes them under D6's
// commit (D8's `linkat` from `.tmp-` names, one D11 sync group, then the table by rename).
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

// Fetches one file by its upstream name from the authority or an upstream relay. Throws on failure.
using MirrorFetch = std::function<std::string(const MirrorFile &)>;
struct AdoptStats {
	std::uint64_t fetched_files = 0, fetched_bytes = 0, reused_files = 0, collected_files = 0;
};
class FetchRefused : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};

// Qualification hook: runs after every primitive of startup, adoption, repair and collection, naming it.
// Install before opening a mirror, never concurrently with one.
struct MirrorTesting {
	static void install(void (*)(void *, const char *) noexcept, void *) noexcept;
};

// The mirror's one writer. It holds `owner.lock` exclusively for its lifetime; the directory holds
// nothing else of anyone's.
class MirrorStore {
  public:
	struct Options {
		unsigned mode = 0644;
		std::uint64_t maximum_table = std::uint64_t(1) << 30;
	};
	// Takes `owner.lock`, then D11's startup: one barrier, `.tmp-` names removed, the table read, each
	// reached file checked by name and size (never re-hashed: D12.2; digests are checked at first read),
	// and every name the table does not reach removed. A table that does not decode refuses the mirror;
	// a namespace with a file missing or of the wrong size is failed until it is adopted again.
	MirrorStore(std::shared_ptr<kronuz::io::completion::PosixIO>, Options);
	~MirrorStore();
	MirrorStore(const MirrorStore &) = delete;
	MirrorStore &operator=(const MirrorStore &) = delete;
	const MirrorTable &table() const noexcept;
	std::span<const std::string> failed() const noexcept;
	// Adopts each namespace's layout (replacing its entry) and drops each name in `dropped`. Files the
	// durable table reaches and holds are reused by name and never fetched; every other file is fetched,
	// checked against its size and digest, written under a `.tmp-` name and linked to its mirror name.
	// The fetched files and the directory sync as one group; then the table is written under a `.tmp-`
	// name, fsynced, renamed over `mirror` and the directory synced; then every file the new table no
	// longer reaches is unlinked. Nothing is published when a fetch fails or disagrees (FetchRefused),
	// and nothing is written when the durable table already holds the layout.
	AdoptStats adopt(std::span<const NamespaceLayout>, std::span<const std::string> dropped, const MirrorFetch &);
	// The bytes of a file the table reaches. Its digest is checked at its first read in this process;
	// a file that fails is fetched again by name, replaced durably, and its new bytes returned.
	std::string read(const MirrorFile &, const MirrorFetch &);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::relay
