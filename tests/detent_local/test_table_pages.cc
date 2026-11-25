// `selected` in pages: a read verifies only the pages it reaches (docs/perf/first-read-1ms.md, "Design:
// table pages"); one test per lifecycle row, and the bound that keeps every entry inside one page.
#include "detent_distribution/storage.hh"
#include "detent_local/disk_store.hh"
#include "detent_local/generation.hh"
#include "detent_local/store_table.hh"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sha256.h>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
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

// 'L' for LayoutRefused, 'T' for any other TableRefused, '-' for an answer.
template <class F> char outcome(F &&f) {
	try {
		f();
	} catch (const l::LayoutRefused &) {
		return 'L';
	} catch (const l::TableRefused &) {
		return 'T';
	}
	return '-';
}
// A table of an older layout (1 or 2): a 64-byte header, entries of a length-prefixed name and 136
// fixed bytes, then the SHA-256 of everything before it.
std::vector<char> older_table(unsigned layout, std::size_t entries) {
	std::vector<char> bytes(64, '\0');
	std::copy_n("DTTABLE1", 8, bytes.begin());
	bytes[11] = static_cast<char>(layout);
	bytes[15] = static_cast<char>(entries);
	for (std::size_t i = 0; i < entries; ++i) {
		const auto name = name_of(i);
		bytes.push_back(static_cast<char>(name.size()));
		bytes.insert(bytes.end(), name.begin(), name.end());
		bytes.insert(bytes.end(), 136, '\1');
	}
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<unsigned char, 32> sum{};
	hash.getHash(sum.data());
	bytes.insert(bytes.end(), sum.begin(), sum.end());
	return bytes;
}

// An older layout's whole table is LayoutRefused at every size its encoder writes, the empty one's 96
// bytes included (D43); a shorter or damaged one is damage.
void older_layouts() {
	for (const unsigned layout : {1u, 2u}) {
		const auto empty = older_table(layout, 0), full = older_table(layout, 3);
		check(empty.size() == 96 && full.size() == 96 + 3 * 145, "older table sizes");
		check(outcome([&] { l::PagedTable{std::vector<char>(empty)}; }) == 'L', "an empty older table");
		check(outcome([&] { l::PagedTable{std::vector<char>(full)}; }) == 'L', "a full older table");
		check(outcome([&] { l::decode_table(empty); }) == 'L', "the whole read of an empty older table");
		for (const std::size_t size : {std::size_t(12), std::size_t(64), std::size_t(95)})
			check(outcome([&] { l::PagedTable{std::vector<char>(empty.begin(), empty.begin() + size)}; }) == 'T',
			      "a short older table is damage");
		auto damaged = empty;
		damaged[20] ^= 1;
		check(outcome([&] { l::PagedTable{std::move(damaged)}; }) == 'T', "a damaged older table is damage");
	}
	// The store read the agent's open relies on: an empty layout-2 `selected` is refused as a layout.
	const auto store = std::filesystem::current_path() / ".scratch" / ("table-pages-" + std::to_string(::getpid()));
	std::filesystem::remove_all(store);
	std::filesystem::create_directories(store);
	std::filesystem::permissions(store, std::filesystem::perms::owner_all);
	{
		const auto empty = older_table(2, 0);
		std::ofstream out(store / "selected", std::ios::binary);
		out.write(empty.data(), std::streamsize(empty.size()));
	}
	std::filesystem::permissions(store / "selected", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
	bool layout = false;
	try {
		l::read_generation_selection(store);
	} catch (const l::LayoutRefused &) {
		layout = true;
	} catch (const std::exception &) {
	}
	check(layout, "the store's read refuses an empty layout-2 table as a layout");
	// A managed store over it opens, refuses the layout once and removes the table (D43).
	auto baseline = std::make_shared<d::StorageLedger>(std::size_t(1) << 20);
	auto account = std::make_shared<d::StorageLedger>(std::size_t(256) << 20);
	d::RetirementRuntime runtime(baseline, 2);
	const auto opened = [&](l::StoreOpenMode mode, auto &&body) {
		auto domain = runtime.domain(account, 64);
		l::DiskStoreOptions options;
		options.mode = mode;
		options.limits = {16 << 20, 100, 10};
		auto disk = l::make_disk_store(domain, store, std::make_shared<int>(1), options);
		body(*disk);
		disk->close();
		for (unsigned turns = 0;; ++turns) {
			check(turns < 100000, "the store closes");
			disk->advance();
			auto work = disk->work();
			if (!work)
				break;
			work->run();
		}
		disk.reset();
		domain.close().wait();
	};
	std::filesystem::remove_all(store);
	std::filesystem::create_directories(store);
	std::filesystem::permissions(store, std::filesystem::perms::owner_all);
	opened(l::StoreOpenMode::FreshEmpty, [](l::DiskStore &) {});
	{
		const auto empty = older_table(2, 0);
		std::ofstream out(store / "selected", std::ios::binary);
		out.write(empty.data(), std::streamsize(empty.size()));
	}
	std::filesystem::permissions(store / "selected", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
	opened(l::StoreOpenMode::ExistingManaged, [](l::DiskStore &disk) {
		check(disk.status() == l::StoreStatus::Ready && disk.refusal() &&
		          disk.selection().knowledge == l::SelectionKnowledge::Absent,
		      "a managed store over an empty layout-2 table opens empty, the layout refused");
	});
	check(!std::filesystem::exists(store / "selected"), "the refused table is removed");
	runtime.shutdown();
	std::filesystem::remove_all(store);
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

// Threads first touching one page together each answer its entry (both may hash it; both marks agree),
// and each refuses a damaged page; a page marked by any of them is not hashed again.
void concurrent_first_touch() {
	const auto table = table_of(1000);
	const auto encoded = l::encode_table(table);
	auto damaged = bytes_of(encoded);
	damaged[entry_at(damaged, name_of(999)) + l::table_entry_bytes(8) - 1] ^= 1;
	for (unsigned round = 0; round != 50; ++round) {
		const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
		const l::PagedTable fresh(bytes_of(encoded), account), bad(std::vector<char>(damaged), account);
		std::atomic<unsigned> wrong{0};
		std::vector<std::thread> threads;
		for (unsigned t = 0; t != 8; ++t)
			threads.emplace_back([&] {
				if (fresh.find(name_of(500)) != table.entries[500])
					++wrong;
				if (!refuses([&] { bad.find(name_of(999)); }))
					++wrong;
				if (bad.find(name_of(10)) != table.entries[10])
					++wrong;
			});
		for (auto &thread : threads)
			thread.join();
		check(wrong == 0, "every concurrent first touch answers or refuses");
		const auto hashed = fresh.page_verifications();
		check(hashed >= 1 && hashed <= 8, "a page hashed by at most each first toucher");
		check(fresh.find(name_of(501)) == table.entries[501] && fresh.page_verifications() == hashed,
		      "a page marked by a concurrent touch is not hashed again");
	}
}

// Every single-byte change, truncation and extension of a three-page table: each lookup, before and
// after its page is marked, answers the original entry, answers not listed for an unlisted name, or
// refuses; the whole read equals the table or refuses.
void mutations() {
	const auto table = table_of(70);
	const auto encoded = bytes_of(l::encode_table(table));
	std::size_t refused = 0, cases = 0;
	const auto oracle = [&](std::vector<char> bytes) {
		++cases;
		const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
		std::optional<l::PagedTable> paged;
		try {
			paged.emplace(std::move(bytes), account);
		} catch (const l::TableRefused &) {
			++refused;
			return;
		}
		// Each page's first and last entry and an unlisted name, before and after the marks.
		for (unsigned pass = 0; pass != 2; ++pass)
			for (const std::size_t i : {0, 27, 28, 55, 56, 69, 70}) {
				const bool listed = i < table.entries.size();
				std::optional<l::TableEntry> got;
				try {
					got = paged->find(listed ? name_of(i) : std::string("0000006x"));
				} catch (const l::TableRefused &) {
					continue;
				}
				check(listed ? got == table.entries[i] : !got, "a mutated table answers only the original entries");
			}
		try {
			check(paged->decode() == table, "a mutated table's whole read is the table");
		} catch (const l::TableRefused &) {
		}
	};
	check(l::PagedTable(std::vector<char>(encoded)).decode() == table, "the table reads whole");
	check(encoded[entry_at(encoded, name_of(28))] == 8 && entry_at(encoded, name_of(28)) % l::table_page_bytes ==
	          entry_at(encoded, name_of(0)) % l::table_page_bytes, "28 entries a page: three pages");
	for (std::size_t at = 0; at < encoded.size(); ++at) {
		auto bytes = encoded;
		bytes[at] ^= 1;
		oracle(std::move(bytes));
		oracle(std::vector<char>(encoded.begin(), encoded.begin() + std::ptrdiff_t(at)));
	}
	for (const char extra : {'\0', '\1'}) {
		auto bytes = encoded;
		bytes.push_back(extra);
		oracle(std::move(bytes));
	}
	std::printf("table mutations: %zu cases, %zu refused at the read\n", cases, refused);
}
} // namespace

int main() {
	older_layouts();
	attach();
	first_reads();
	corrupt_page();
	publish();
	close();
	bound();
	concurrent_first_touch();
	mutations();
	std::puts("table pages: ok");
}
