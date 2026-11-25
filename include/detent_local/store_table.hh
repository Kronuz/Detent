#pragma once
#include "detent/cursor.hh"
#include "detent_local/generation.hh"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// `selected`, the agent store's namespace table (docs/status/namespace-s7.md): a pure codec that
// readers and the store share.
namespace detent::distribution {
class StorageLedger;
}
namespace detent::local {
namespace detail {
class VerifiedPages;
}
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
// An entry of a verified table page, read in place, or none: like main's `const TableEntry *`, it borrows
// the PagedTable it came from and is valid as long as that table. A caller that keeps it copies it.
class TableEntryView {
  public:
	TableEntryView() = default;
	explicit operator bool() const noexcept { return at_ != nullptr; }
	std::string_view name() const noexcept { return {at_ + 1, static_cast<unsigned char>(*at_)}; }
	NamespaceCursor cursor() const noexcept;
	std::uint32_t slot() const noexcept;
	GenerationId generation() const noexcept;
	// The entry, copied.
	TableEntry entry() const;
	// False for no entry.
	bool operator==(const TableEntry &other) const { return at_ && entry() == other; }

  private:
	friend class PagedTable;
	explicit TableEntryView(const char *at) noexcept : at_(at) {}
	const char *at_ = nullptr; // the entry's length byte
};
// `selected`: replaced atomically. Entries are sorted by name, in pages of whole entries
// (docs/LOCAL_IMAGE_FORMAT.md).
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
// A whole store of an older layout: a table of an older layout, or a root it names in an older format.
// No other layout is read (D43): the agent refuses the store once and bootstraps a fresh copy. A newer
// layout is not this agent's to remove: it fences like damage.
class LayoutRefused : public TableRefused {
  public:
	using TableRefused::TableRefused;
};
inline constexpr std::size_t table_page_bytes = 4096;
// An entry's encoding: its name's length byte and bytes, then 136 bytes of fields.
constexpr std::size_t table_entry_bytes(std::size_t name) noexcept { return 137 + name; }
// The longest entry fits a page, so the writer never splits an entry across two.
static_assert(table_entry_bytes(255) < table_page_bytes);

// A read of `selected` that verifies only what it reads: the header, the index and the table digest
// over them at construction, and each page when a lookup first reaches it, as the trie's first touch
// verifies a page. With an account, a verified page is marked (VerifiedPages) and not hashed again.
class PagedTable {
  public:
	// Throws TableRefused, LayoutRefused for a whole table of an older layout.
	explicit PagedTable(std::vector<char> bytes, std::shared_ptr<distribution::StorageLedger> account = {});
	// The table `size` bytes long open at `fd`, mapped read-only as segments are: a read touches only the
	// header, the index and the pages it reaches. Throws std::system_error when it cannot map.
	static PagedTable map(int fd, std::size_t size, std::shared_ptr<distribution::StorageLedger> account = {});
	PagedTable(PagedTable &&) noexcept;
	PagedTable &operator=(PagedTable &&) noexcept;
	~PagedTable();
	std::uint64_t sequence() const noexcept { return sequence_; }
	const DirectoryCursor &directory() const noexcept { return directory_; }
	const distribution::Profile &profile() const noexcept { return profile_; }
	std::uint64_t size() const noexcept { return count_; }
	// The highest slot an entry names, plus one.
	std::uint32_t slots() const noexcept { return slots_; }
	// The entry `name` in place, or none when the table does not list it. Throws TableRefused when its page
	// is damaged.
	TableEntryView find(std::string_view name) const;
	// Every entry, every page verified, with the checks that span pages.
	StoreTable decode() const;
	// Pages verified so far (hashed, digest matching); a marked page is not hashed again.
	std::size_t page_verifications() const noexcept;
	// The table digest commits to the header and every page.
	bool operator==(const PagedTable &other) const noexcept { return digest_ == other.digest_; }

  private:
	struct Page {
		std::size_t offset, size, index; // the page's bytes; its index record (digest, last name)
		std::string_view last;
	};
	struct State;
	struct Mapping {
		void *at = nullptr;
		std::size_t size = 0;
		Mapping() = default;
		Mapping(Mapping &&other) noexcept : at(std::exchange(other.at, nullptr)), size(other.size) {}
		Mapping &operator=(Mapping &&other) noexcept;
		~Mapping();
	};
	PagedTable(std::vector<char> owned, Mapping mapping, std::shared_ptr<distribution::StorageLedger> account);
	// Verifies page `page` (hashed unless marked, every entry check), puts each entry's offset in `offsets`
	// and, with an account, keeps them with the page's mark. Returns the entry count.
	std::size_t check(std::size_t page, std::span<std::uint16_t> offsets, std::vector<TableEntry> *all) const;

	std::vector<char> owned_;
	Mapping mapping_;
	std::span<const char> bytes_; // owned_'s or mapping_'s
	std::uint64_t sequence_ = 0, count_ = 0;
	DirectoryCursor directory_;
	distribution::Profile profile_;
	std::uint32_t slots_ = 0;
	std::size_t pages_begin_ = 0;
	std::array<char, 32> digest_{};
	std::vector<Page> pages_;
	std::unique_ptr<State> state_;
};
std::string encode_table(const StoreTable &);
// Throws TableRefused for anything but a whole, current table.
StoreTable decode_table(std::span<const char>);

// The bound on a table's encoding: its header, `entries` entries of the longest name, and its digest.
std::uint64_t table_bytes(std::uint64_t entries) noexcept;
} // namespace detent::local
