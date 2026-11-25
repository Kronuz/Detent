#include "detent_local/store_table.hh"
#include "detent_local/derived_ids.hh"
#include "detent_local/layout.hh"
#include "detent_local/read_counters.hh"
#include "verified_pages.hh"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <sha256.h>
#include <system_error>
#include <sys/mman.h>

namespace detent::local {
namespace {
using Sha = std::array<char, 32>;
constexpr std::string_view table_magic = "DTTABLE1";
// The header, and an older layout's; an index record's digest and name length; the longest entry.
constexpr std::size_t table_header = 72, old_header = 64, index_fixed = 33, entry_max = table_entry_bytes(255),
					  maximum_entries = 1u << 20;
Sha digest(std::span<const char> bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	Sha out{};
	hash.getHash(reinterpret_cast<unsigned char *>(out.data()));
	return out;
}
bool valid_name(std::string_view name) {
	return !name.empty() && name.size() <= 255 && name.find('\0') == std::string_view::npos;
}
template <std::size_t N> bool nonzero(const std::array<char, N> &value) {
	return std::any_of(value.begin(), value.end(), [](char byte) { return byte != 0; });
}
void refuse(bool ok, const char *what) {
	if (!ok)
		throw TableRefused(std::string("agent store table: ") + what);
}
template <std::size_t N> void put(std::string &out, std::size_t &at, const std::array<char, N> &value) {
	std::copy(value.begin(), value.end(), out.begin() + at);
	at += N;
}
void put(std::string &out, std::size_t &at, std::uint64_t value, unsigned width) {
	write_integer(std::span<char>(out), at, value, width);
	at += width;
}
template <std::size_t N> void take(std::span<const char> in, std::size_t &at, std::array<char, N> &value) {
	std::copy_n(in.begin() + at, N, value.begin());
	at += N;
}
std::uint64_t take(std::span<const char> in, std::size_t &at, unsigned width) {
	const auto value = read_integer(in, at, width);
	at += width;
	return value;
}
// An entry's fields after its name: cluster, incarnation, position, slot, generation, ordinal, root
// length, root, read.
constexpr std::size_t position_at = 32, slot_at = 40, generation_at = 44, ordinal_at = 60, root_length_at = 64,
                      root_at = 72, read_at = 104;
const char *fields_of(const char *at) noexcept { return at + 1 + static_cast<unsigned char>(*at); }
std::uint64_t field(const char *at, std::size_t offset, unsigned width) noexcept {
	std::uint64_t value = 0;
	for (const auto *byte = fields_of(at) + offset; width; --width)
		value = (value << 8) | static_cast<unsigned char>(*byte++);
	return value;
}
template <std::size_t N> std::array<char, N> bytes_at(const char *at, std::size_t offset) noexcept {
	std::array<char, N> value;
	std::copy_n(fields_of(at) + offset, N, value.begin());
	return value;
}
// The entry whose length byte is at `at`, on a checked page.
TableEntry entry_at(const char *at) {
	TableEntry entry;
	entry.name.assign(at + 1, static_cast<unsigned char>(*at));
	std::copy_n(fields_of(at), 16, entry.cursor.cluster.begin());
	std::copy_n(fields_of(at) + 16, 16, entry.cursor.incarnation.begin());
	entry.cursor.position = field(at, position_at, 8);
	entry.slot = static_cast<std::uint32_t>(field(at, slot_at, 4));
	entry.generation = bytes_at<16>(at, generation_at);
	entry.ordinal = static_cast<std::uint32_t>(field(at, ordinal_at, 4));
	entry.root_length = field(at, root_length_at, 8);
	entry.root = bytes_at<32>(at, root_at);
	entry.read = bytes_at<32>(at, read_at);
	return entry;
}
} // namespace

NamespaceCursor TableEntryView::cursor() const noexcept {
	return {bytes_at<16>(at_, 0), bytes_at<16>(at_, 16), field(at_, position_at, 8)};
}
std::uint32_t TableEntryView::slot() const noexcept { return static_cast<std::uint32_t>(field(at_, slot_at, 4)); }
GenerationId TableEntryView::generation() const noexcept { return bytes_at<16>(at_, generation_at); }
TableEntry TableEntryView::entry() const { return entry_at(at_); }

namespace {
// An entry of a page, checked; `end` bounds it.
TableEntry take_entry(std::span<const char> in, std::size_t &at, std::size_t end) {
	const auto length = static_cast<unsigned char>(in[at]);
	refuse(length && end - at > table_entry_bytes(length) - 1, "truncated entry");
	auto entry = entry_at(in.data() + at);
	at += table_entry_bytes(length);
	refuse(valid_name(entry.name) && nonzero(entry.generation), "entry");
	return entry;
}
void put_entry(std::string &out, std::size_t &at, const TableEntry &entry) {
	put(out, at, entry.name.size(), 1);
	std::copy(entry.name.begin(), entry.name.end(), out.begin() + at);
	at += entry.name.size();
	put(out, at, entry.cursor.cluster);
	put(out, at, entry.cursor.incarnation);
	put(out, at, entry.cursor.position, 8);
	put(out, at, entry.slot, 4);
	put(out, at, entry.generation);
	put(out, at, entry.ordinal, 4);
	put(out, at, entry.root_length, 8);
	put(out, at, entry.root);
	put(out, at, entry.read);
}
} // namespace

// Each page carries an index record of at most 288 bytes and pads less than one entry.
std::uint64_t table_bytes(std::uint64_t entries) noexcept {
	return table_header + 32 + entries * (2 * entry_max + index_fixed + 255);
}
const TableEntry *StoreTable::find(std::string_view name) const noexcept {
	auto at = std::lower_bound(entries.begin(), entries.end(), name,
							   [](const TableEntry &entry, std::string_view key) { return entry.name < key; });
	return at != entries.end() && at->name == name ? &*at : nullptr;
}

std::string encode_table(const StoreTable &table) {
	if (table.entries.size() > maximum_entries)
		throw std::length_error("agent store table entries");
	// Pages of whole entries: a page closes before the entry that would overflow it.
	std::vector<std::pair<std::size_t, std::size_t>> pages; // [first, last] entry
	std::size_t used = table_page_bytes, index = 0, body = 0, slots = 0;
	for (std::size_t i = 0; i < table.entries.size(); ++i) {
		const auto &entry = table.entries[i];
		if (!valid_name(entry.name) || (i && !(table.entries[i - 1].name < entry.name)) || !nonzero(entry.generation))
			throw std::invalid_argument("agent store table entry");
		const auto bytes = table_entry_bytes(entry.name.size());
		if (used + bytes > table_page_bytes) {
			if (!pages.empty()) {
				index += index_fixed + table.entries[pages.back().second].name.size();
				body += table_page_bytes;
			}
			pages.push_back({i, i});
			used = 0;
		}
		pages.back().second = i;
		used += bytes;
		slots = std::max<std::size_t>(slots, std::size_t(entry.slot) + 1);
	}
	if (!pages.empty()) {
		index += index_fixed + table.entries[pages.back().second].name.size();
		body += used;
	}
	std::string out(table_header + index + 32 + body, '\0');
	std::size_t at = 0;
	std::copy(table_magic.begin(), table_magic.end(), out.begin());
	at = table_magic.size();
	put(out, at, agent_store_layout, 4);
	put(out, at, table.entries.size(), 4);
	put(out, at, table.sequence, 8);
	put(out, at, table.directory.cluster);
	put(out, at, table.directory.position, 8);
	put(out, at, table.profile.semantic_id, 4);
	put(out, at, table.profile.required_capabilities, 8);
	put(out, at, pages.size(), 4);
	put(out, at, slots, 4);
	at = table_header;
	std::size_t page_at = table_header + index + 32;
	for (const auto &[first, last] : pages) {
		std::size_t end = page_at;
		for (auto i = first; i <= last; ++i)
			put_entry(out, end, table.entries[i]);
		const auto page_size = last == pages.back().second ? end - page_at : table_page_bytes;
		put(out, at, digest({out.data() + page_at, page_size}));
		const auto &name = table.entries[last].name;
		put(out, at, name.size(), 1);
		std::copy(name.begin(), name.end(), out.begin() + at);
		at += name.size();
		page_at += page_size;
	}
	put(out, at, digest({out.data(), at}));
	return out;
}

// A page's index, kept with its mark: its entry count, then each entry's offset in the page. Written
// before the mark's release and read after its acquire, so a marked page is searched without re-checking.
constexpr std::size_t page_entries = table_page_bytes / table_entry_bytes(1), page_index = 1 + page_entries;
using PageOffsets = std::array<std::uint16_t, page_entries>;
static_assert((page_entries + 1) * table_entry_bytes(1) > table_page_bytes && table_page_bytes <= UINT16_MAX);
using Offsets = std::vector<std::atomic<std::uint16_t>, distribution::StorageAllocator<std::atomic<std::uint16_t>>>;
struct PagedTable::State {
	std::unique_ptr<detail::VerifiedPages> marks; // absent without an account: every reach hashes
	std::unique_ptr<Offsets> offsets;             // page_index per page, beside the marks
	std::atomic<std::size_t> verified{0};
};

PagedTable::Mapping &PagedTable::Mapping::operator=(Mapping &&other) noexcept {
	if (this != &other) {
		if (at)
			::munmap(at, size);
		at = std::exchange(other.at, nullptr);
		size = other.size;
	}
	return *this;
}
PagedTable::Mapping::~Mapping() {
	if (at)
		::munmap(at, size);
}
PagedTable PagedTable::map(int fd, std::size_t size, std::shared_ptr<distribution::StorageLedger> account) {
	Mapping mapping;
	if (size) {
		const auto at = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (at == MAP_FAILED)
			throw std::system_error(errno, std::generic_category(), "selected mmap");
		mapping.at = at;
		mapping.size = size;
	}
	return PagedTable({}, std::move(mapping), std::move(account));
}
PagedTable::PagedTable(std::vector<char> bytes, std::shared_ptr<distribution::StorageLedger> account)
	: PagedTable(std::move(bytes), Mapping(), std::move(account)) {}
PagedTable::PagedTable(std::vector<char> owned, Mapping mapping, std::shared_ptr<distribution::StorageLedger> account)
	: owned_(std::move(owned)), mapping_(std::move(mapping)),
	  bytes_(mapping_.at ? std::span<const char>(static_cast<const char *>(mapping_.at), mapping_.size)
	                     : std::span<const char>(owned_)),
	  state_(std::make_unique<State>()) {
	const std::span<const char> in(bytes_);
	// The layout comes first: every size rule after it is its layout's.
	refuse(in.size() >= 12 && std::string_view(in.data(), 8) == table_magic, "header");
	std::size_t at = 8;
	const auto layout = take(in, at, 4);
	if (layout < agent_store_layout) {
		// An older layout: a 64-byte header, its entries, and one trailing digest of the whole table.
		refuse(in.size() >= old_header + 32, "header");
		const auto sum = digest(in.first(in.size() - 32));
		refuse(std::equal(sum.begin(), sum.end(), in.end() - 32), "digest");
		throw LayoutRefused("agent store table: layout");
	}
	refuse(layout == agent_store_layout, "layout");
	refuse(in.size() >= table_header + 32, "header");
	count_ = take(in, at, 4);
	sequence_ = take(in, at, 8);
	take(in, at, directory_.cluster);
	directory_.position = take(in, at, 8);
	profile_.semantic_id = static_cast<std::uint32_t>(take(in, at, 4));
	profile_.required_capabilities = take(in, at, 8);
	const auto pages = take(in, at, 4);
	slots_ = static_cast<std::uint32_t>(take(in, at, 4));
	refuse(std::all_of(in.begin() + at, in.begin() + table_header, [](char byte) { return byte == 0; }), "padding");
	refuse(count_ <= maximum_entries && pages <= count_ && (pages == 0) == (count_ == 0), "entry count");
	at = table_header;
	pages_.reserve(pages);
	for (std::uint64_t k = 0; k < pages; ++k) {
		refuse(in.size() - at >= index_fixed + 32, "truncated index");
		const auto index = at;
		at += 32;
		const auto length = static_cast<std::size_t>(take(in, at, 1));
		refuse(length && in.size() - at >= length + 32, "truncated index");
		const std::string_view last(in.data() + at, length);
		at += length;
		refuse(valid_name(last) && (pages_.empty() || pages_.back().last < last), "index order");
		pages_.push_back({0, 0, index, last});
	}
	DETENT_COUNT(bytes_hashed, at);
	const auto sum = digest(in.first(at));
	refuse(std::equal(sum.begin(), sum.end(), in.begin() + at), "digest");
	std::copy_n(in.begin() + at, 32, digest_.begin());
	pages_begin_ = at + 32;
	const auto body = in.size() - pages_begin_;
	refuse(pages ? body > (pages - 1) * table_page_bytes && body <= pages * table_page_bytes : body == 0, "page bytes");
	for (std::size_t k = 0; k < pages_.size(); ++k) {
		pages_[k].offset = pages_begin_ + k * table_page_bytes;
		pages_[k].size = std::min(table_page_bytes, in.size() - pages_[k].offset);
	}
	if (account && body) {
		state_->marks = std::make_unique<detail::VerifiedPages>(body, account);
		state_->offsets = std::make_unique<Offsets>(pages_.size() * page_index,
		                                            distribution::StorageAllocator<std::atomic<std::uint16_t>>(account));
	}
}
PagedTable::PagedTable(PagedTable &&) noexcept = default;
PagedTable &PagedTable::operator=(PagedTable &&) noexcept = default;
PagedTable::~PagedTable() = default;

std::size_t PagedTable::page_verifications() const noexcept { return state_->verified.load(std::memory_order_relaxed); }

std::size_t PagedTable::check(std::size_t k, std::span<std::uint16_t> offsets, std::vector<TableEntry> *all) const {
	const auto &page = pages_[k];
	const std::span<const char> in(bytes_.data() + page.offset, page.size);
	const auto &marks = state_->marks;
	const bool marked = marks && marks->test(page.offset - pages_begin_);
	if (!marked) {
		DETENT_COUNT(bytes_hashed, in.size());
		const auto sum = digest(in);
		refuse(std::equal(sum.begin(), sum.end(), bytes_.begin() + page.index), "page digest");
		state_->verified.fetch_add(1, std::memory_order_relaxed);
	}
	// Entries in order after the previous page's last name, each slot below `slots`, the index's last
	// name last, then zero padding.
	std::size_t count = 0, at = 0;
	std::string_view previous = k ? pages_[k - 1].last : std::string_view();
	while (at < in.size() && in[at] != 0) {
		const auto start = at;
		auto entry = take_entry(in, at, in.size());
		refuse(previous < entry.name && entry.slot < slots_, "entry order");
		previous = std::string_view(in.data() + start + 1, entry.name.size());
		offsets[count++] = static_cast<std::uint16_t>(start);
		if (all)
			all->push_back(std::move(entry));
	}
	refuse(at && previous == page.last, "page end");
	refuse(std::all_of(in.begin() + at, in.end(), [](char byte) { return byte == 0; }), "page padding");
	if (marks && !marked) {
		auto *index = state_->offsets->data() + k * page_index;
		index[0].store(static_cast<std::uint16_t>(count), std::memory_order_relaxed);
		for (std::size_t i = 0; i < count; ++i)
			index[1 + i].store(offsets[i], std::memory_order_relaxed);
		marks->set(page.offset - pages_begin_);
	}
	return count;
}

TableEntryView PagedTable::find(std::string_view name) const {
	const auto at = std::lower_bound(pages_.begin(), pages_.end(), name,
	                                 [](const Page &page, std::string_view key) { return page.last < key; });
	if (at == pages_.end())
		return {};
	const auto k = std::size_t(at - pages_.begin());
	const std::span<const char> in(bytes_.data() + at->offset, at->size);
	// A marked page answers from its index; any other reach checks the page into a local one.
	PageOffsets local;
	const std::atomic<std::uint16_t> *index = nullptr;
	std::size_t count;
	if (state_->marks && state_->marks->test(at->offset - pages_begin_)) {
		index = state_->offsets->data() + k * page_index;
		count = index[0].load(std::memory_order_relaxed);
		++index;
	} else {
		count = check(k, local, nullptr);
	}
	const auto offset = [&](std::size_t i) -> std::size_t {
		return index ? index[i].load(std::memory_order_relaxed) : local[i];
	};
	// Names compared in place; the match is answered in place.
	for (std::size_t low = 0, high = count; low < high;) {
		const auto mid = low + (high - low) / 2;
		const TableEntryView entry(in.data() + offset(mid));
		const auto order = entry.name().compare(name);
		if (order < 0)
			low = mid + 1;
		else if (order > 0)
			high = mid;
		else
			return entry;
	}
	return {};
}

StoreTable PagedTable::decode() const {
	StoreTable table;
	table.sequence = sequence_;
	table.directory = directory_;
	table.profile = profile_;
	table.entries.reserve(count_);
	PageOffsets offsets;
	for (std::size_t k = 0; k < pages_.size(); ++k)
		check(k, offsets, &table.entries);
	refuse(table.entries.size() == count_, "entry count");
	std::vector<std::uint32_t> slots;
	slots.reserve(count_);
	for (const auto &entry : table.entries)
		slots.push_back(entry.slot);
	std::sort(slots.begin(), slots.end());
	refuse(std::adjacent_find(slots.begin(), slots.end()) == slots.end(), "duplicate slot");
	refuse(slots_ == (slots.empty() ? 0 : slots.back() + 1), "slots");
	return table;
}

StoreTable decode_table(std::span<const char> in) { return PagedTable({in.begin(), in.end()}).decode(); }

} // namespace detent::local
