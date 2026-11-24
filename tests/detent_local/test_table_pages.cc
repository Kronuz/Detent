// `selected` in pages: a read verifies only the pages it reaches (docs/perf/first-read-1ms.md, "Design:
// table pages"); one test per lifecycle row, and the bound that keeps every entry inside one page.
#include "detent_distribution/storage.hh"
#include "detent_local/store_table.hh"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace d = detent::distribution;
namespace l = detent::local;
namespace {
void check(bool ok, const char *what) {
	if (!ok) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		std::exit(1);
	}
}
template <class F> bool refuses(F &&f) {
	try {
		f();
	} catch (const l::TableRefused &) {
		return true;
	}
	return false;
}
std::string name_of(std::size_t i, std::size_t width = 8) {
	auto name = std::to_string(i);
	return std::string(width - name.size(), '0') + name;
}
l::StoreTable table_of(std::size_t count, std::size_t width = 8, std::uint64_t sequence = 7) {
	l::StoreTable table;
	table.sequence = sequence;
	table.directory.position = 3;
	table.profile.semantic_id = 5;
	for (std::size_t i = 0; i < count; ++i) {
		l::TableEntry entry;
		entry.name = name_of(i, width);
		entry.cursor.position = sequence * 1000 + i;
		entry.slot = static_cast<std::uint32_t>(count - 1 - i);
		entry.generation[0] = 1;
		entry.generation[15] = static_cast<char>(i);
		entry.root_length = 320;
		entry.root[0] = static_cast<char>(i);
		entry.read[31] = static_cast<char>(i + 1);
		table.entries.push_back(entry);
	}
	return table;
}
std::vector<char> bytes_of(const std::string &encoded) { return {encoded.begin(), encoded.end()}; }
// The offset of `name`'s entry: its length byte precedes it in its page.
std::size_t entry_at(const std::vector<char> &bytes, std::string_view name) {
	const std::string_view all(bytes.data(), bytes.size());
	const auto at = all.rfind(name); // an index record may name it too; the entry follows the index
	check(at != std::string_view::npos && bytes[at - 1] == char(name.size()), "entry located");
	return at - 1;
}

// Attach: the header, index and table digest are checked; no page is hashed.
void attach() {
	const auto table = table_of(1000);
	const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
	const l::PagedTable paged(bytes_of(l::encode_table(table)), account);
	check(paged.sequence() == 7 && paged.size() == 1000 && paged.slots() == 1000, "header fields");
	check(paged.profile().semantic_id == 5 && paged.directory().position == 3, "header cursor and profile");
	check(paged.page_verifications() == 0, "no page hashed at attach");
	check(account->used() > 0, "the marks are accounted");
	// A damaged index refuses the table at its read.
	auto damaged = bytes_of(l::encode_table(table));
	damaged[72 + 5] ^= 1; // inside the first index record's page digest
	check(refuses([&] { l::PagedTable(std::move(damaged)); }), "a damaged index refuses the read");
}

// The first read of an entry hashes its page once and answers the entry; another entry on that page
// hashes nothing; an entry on another page hashes that page.
void first_reads() {
	const auto table = table_of(1000);
	const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
	const l::PagedTable paged(bytes_of(l::encode_table(table)), account);
	check(paged.find(name_of(500)) == table.entries[500], "first read answers its entry");
	check(paged.page_verifications() == 1, "first read hashes one page");
	check(paged.find(name_of(501)) == table.entries[501], "a neighbour answers its entry");
	check(paged.page_verifications() == 1, "a verified page is not hashed again");
	check(paged.find(name_of(10)) == table.entries[10] && paged.page_verifications() == 2, "another page");
	check(!paged.find("0000099x") && !paged.find("zzzz") && !paged.find("a"), "unlisted names");
	check(paged.decode() == table, "the whole read equals the table");
	// Without an account nothing is marked: each reach hashes.
	const l::PagedTable unmarked(bytes_of(l::encode_table(table)));
	unmarked.find(name_of(500));
	unmarked.find(name_of(501));
	check(unmarked.page_verifications() == 2, "unmarked pages hash on every reach");
}

// A corrupt page refuses each read that reaches it and stays unmarked; other pages keep reading.
void corrupt_page() {
	const auto table = table_of(1000);
	auto bytes = bytes_of(l::encode_table(table));
	// The last byte of entry 500's `.read` digest: framing and order still decode.
	bytes[entry_at(bytes, name_of(500)) + l::table_entry_bytes(8) - 1] ^= 1;
	const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
	const l::PagedTable paged(std::move(bytes), account);
	check(refuses([&] { paged.find(name_of(500)); }), "the corrupt page refuses its entry");
	check(refuses([&] { paged.find(name_of(501)); }), "and every entry on it, again");
	check(paged.find(name_of(10)) == table.entries[10], "another page keeps reading");
	check(paged.find(name_of(999)) == table.entries[999], "the last page keeps reading");
	check(refuses([&] { paged.decode(); }), "the whole read refuses");
}

// Publish: a new table is a new read, all pages unmarked, answering the new entries.
void publish() {
	const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
	const l::PagedTable before(bytes_of(l::encode_table(table_of(1000))), account);
	before.find(name_of(500));
	const auto next = table_of(1000, 8, 8);
	const l::PagedTable after(bytes_of(l::encode_table(next)), account);
	check(!(before == after), "a new table is another table");
	check(after.page_verifications() == 0, "a new table starts unmarked");
	check(after.find(name_of(500)) == next.entries[500] && after.find(name_of(500))->cursor.position == 8500,
	      "the new table answers the new entry");
}

// Close: the table's marks return to the account.
void close() {
	const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
	{
		const l::PagedTable paged(bytes_of(l::encode_table(table_of(1000))), account);
		paged.find(name_of(500));
		check(account->used() > 0, "open: marks held");
	}
	check(account->used() == 0, "closed: marks released");
}

// The longest entry fits a page: a table of 255-byte names never splits an entry.
void bound() {
	static_assert(l::table_entry_bytes(255) < l::table_page_bytes);
	const auto table = table_of(100, 255);
	const auto encoded = l::encode_table(table);
	check(encoded.size() <= l::table_bytes(100), "the encoding fits its bound");
	const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
	const l::PagedTable paged(bytes_of(encoded), account);
	for (std::size_t i = 0; i < 100; ++i)
		check(paged.find(name_of(i, 255)) == table.entries[i], "every longest entry reads from its page");
	check(paged.decode() == table, "the longest entries round-trip");
	// Ten 392-byte entries fill a page: 100 entries take 10 pages, each hashed once.
	check(paged.page_verifications() == 10, "ten pages");
	check(l::decode_table(l::encode_table(table_of(0))).entries.empty(), "an empty table");
}
} // namespace

int main() {
	attach();
	first_reads();
	corrupt_page();
	publish();
	close();
	bound();
	std::puts("table pages: ok");
}
