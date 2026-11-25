// `selected` in pages: a read verifies only the pages it reaches (docs/perf/first-read-1ms.md, "Design:
// table pages"); one test per lifecycle row, and the bound that keeps every entry inside one page.
#include "detent_distribution/storage.hh"
#include "detent_local/disk_store.hh"
#include "detent_local/generation.hh"
#include "detent_local/layout.hh"
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
#include <span>
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
	check(after.find(name_of(500)) == next.entries[500] && after.find(name_of(500)).cursor().position == 8500,
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

// The verified page index ("Verified page index"): a marked page answers each entry and each unlisted
// name between them from the offsets its first touch kept, hashing nothing; threads first touching
// the pages together leave the same answers; without an account each reach checks into a local index.
void page_index() {
	const auto table = table_of(1000);
	const auto encoded = l::encode_table(table);
	const auto answers = [&](const l::PagedTable &paged) {
		for (std::size_t i = 0; i < table.entries.size(); ++i)
			if (paged.find(name_of(i)) != table.entries[i] || paged.find(name_of(i) + "x"))
				return false;
		return !paged.find("") && !paged.find("0") && !paged.find("zzzz");
	};
	{
		const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
		const l::PagedTable paged(bytes_of(encoded), account);
		check(answers(paged), "first touches answer every entry and unlisted name");
		const auto pages = paged.page_verifications();
		check(pages > 30 && answers(paged) && paged.page_verifications() == pages,
		      "marked pages answer from their index, hashing nothing");
	}
	for (unsigned round = 0; round != 50; ++round) {
		const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
		const l::PagedTable paged(bytes_of(encoded), account);
		std::atomic<unsigned> ready{0}, wrong{0};
		std::vector<std::thread> threads;
		for (unsigned t = 0; t != 8; ++t)
			threads.emplace_back([&, t] {
				for (++ready; ready != 8;)
					std::this_thread::yield();
				for (std::size_t i = t; i < table.entries.size(); i += 8)
					if (paged.find(name_of(i)) != table.entries[i] || paged.find(name_of(i) + "x"))
						++wrong;
			});
		for (auto &thread : threads)
			thread.join();
		check(wrong == 0, "concurrent first touches answer every entry");
		const auto pages = paged.page_verifications();
		check(answers(paged) && paged.page_verifications() == pages, "the pages they marked answer every entry");
	}
	const l::PagedTable unmarked(bytes_of(encoded));
	check(answers(unmarked), "without an account every entry answers");
	// Each listed name, each unlisted one below the last, "" and "0" reach a page and hash it.
	check(unmarked.page_verifications() == 2 * table.entries.size() + 1, "without an account each reach hashes");
}

// A lookup compares names past the prefix its bounds share (correctness review R3-1): the table's
// last names', and within a page the previous page's last name's with its own. Every width up to the
// format's 255, a first page that does not share the later pages' prefix, bytes above 0x7f, and names
// that are prefixes of each other: each entry, each name around it and every prefix of it answers as
// the decoded table's `find` does, marked and unmarked.
void shared_prefixes() {
	std::vector<l::StoreTable> tables;
	for (const std::size_t width : {16, 40, 255}) {
		l::StoreTable table = table_of(0);
		for (std::size_t i = 0; i < 1000; ++i) {
			const auto digits = std::to_string(i * 2);
			l::TableEntry entry = table_of(1).entries[0];
			entry.name = "namespace-" + std::string(width - 10 - digits.size(), '0') + digits;
			entry.slot = static_cast<std::uint32_t>(i);
			table.entries.push_back(entry);
		}
		tables.push_back(table);
	}
	std::uint64_t seed = 0x9e3779b97f4a7c15;
	const auto next = [&](std::size_t bound) {
		seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17;
		return std::size_t(seed % bound);
	};
	const std::string stems[] = {"", "a", std::string(100, 'p'), std::string(100, 'p') + "q", std::string(240, '\xff')};
	for (unsigned round = 0; round != 4; ++round) {
		std::vector<std::string> names;
		for (std::size_t i = 0; i < 600; ++i) {
			// Round 0: the first page starts with names below the later pages' prefix, as long as it.
			auto name = round ? stems[next(std::size(stems))] : i < 5 ? "a" + std::string(99, 'z') : stems[2];
			for (auto extra = 1 + next(round == 3 ? 15 : 6); extra && name.size() < 255; --extra)
				name.push_back("ab\x01\x7f\x80\xff"[next(6)]);
			names.push_back(name);
		}
		std::sort(names.begin(), names.end());
		names.erase(std::unique(names.begin(), names.end()), names.end());
		l::StoreTable table = table_of(0);
		for (std::size_t i = 0; i < names.size(); ++i) {
			l::TableEntry entry = table_of(1).entries[0];
			entry.name = names[i];
			entry.slot = static_cast<std::uint32_t>(i);
			entry.root[1] = static_cast<char>(i);
			table.entries.push_back(entry);
		}
		tables.push_back(table);
	}
	for (const auto &table : tables) {
		const auto encoded = l::encode_table(table);
		check(l::PagedTable(bytes_of(encoded)).decode() == table, "the table decodes");
		std::vector<std::string> keys{"", "\x01", "\xff", std::string(255, '\xff'), "namespace-", "p", "q"};
		for (const auto &entry : table.entries) {
			const auto &name = entry.name;
			keys.push_back(name);
			for (const char byte : {'\x01', 'a', '\xff'})
				keys.push_back(name + byte);
			for (std::size_t cut = name.size() - 1; cut + 1 > 0; cut -= 1 + cut / 4)
				keys.push_back(name.substr(0, cut));
			// One byte up or down at the start, the middle and the end: a name that differs inside a prefix
			// and shares the rest.
			for (const auto at : {std::size_t(0), name.size() / 2, name.size() - 1})
				for (const int step : {-1, 1}) {
					auto changed = name;
					const auto byte = static_cast<unsigned char>(changed[at]) + step;
					if (byte > 0 && byte < 256) {
						changed[at] = static_cast<char>(byte);
						keys.push_back(changed);
					}
				}
		}
		const auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
		const l::PagedTable marked(bytes_of(encoded), account), unmarked(bytes_of(encoded));
		for (unsigned pass = 0; pass != 2; ++pass)
			for (const auto &key : keys) {
				const auto *want = table.find(key);
				for (const auto *paged : {&marked, &unmarked}) {
					if (pass && paged == &unmarked)
						continue; // every reach of an unmarked table is a first touch
					const auto got = paged->find(key);
					if (want ? !(got == *want) : bool(got)) {
						std::fprintf(stderr, "key of %zu bytes in a table of %zu entries\n", key.size(), table.entries.size());
						check(false, "each key answers as the decoded table's find");
					}
				}
			}
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
				l::TableEntryView got;
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

// The table's layout offsets: the index's first record, the pages' first byte, and the page count.
struct Layout {
	std::size_t pages, pages_begin;
};
Layout layout_of(const std::vector<char> &bytes) {
	const auto pages = std::size_t(l::read_integer(std::span<const char>(bytes), 60, 4));
	std::size_t at = 72;
	for (std::size_t k = 0; k < pages; ++k)
		at += 33 + std::size_t(std::uint8_t(bytes[at + 32]));
	return {pages, at + 32};
}
std::array<char, 32> sha(std::span<const char> bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<char, 32> sum{};
	hash.getHash(reinterpret_cast<unsigned char *>(sum.data()));
	return sum;
}
// Signs every page's digest into its index record, then the table digest over the header and index:
// a table damaged this way passes every digest, so only the structural checks can refuse it.
std::vector<char> resign(std::vector<char> bytes) {
	const auto [pages, begin] = layout_of(bytes);
	for (std::size_t k = 0, record = 72; k < pages; ++k) {
		const auto at = begin + k * l::table_page_bytes;
		if (at >= bytes.size())
			break; // a body shorter than its index: only the table digest is signed
		const auto sum = sha(std::span<const char>(bytes).subspan(at, std::min(l::table_page_bytes, bytes.size() - at)));
		std::copy(sum.begin(), sum.end(), bytes.begin() + std::ptrdiff_t(record));
		record += 33 + std::size_t(std::uint8_t(bytes[record + 32]));
	}
	const auto sum = sha(std::span<const char>(bytes).first(begin - 32));
	std::copy(sum.begin(), sum.end(), bytes.begin() + std::ptrdiff_t(begin - 32));
	return bytes;
}

// Each structural check refuses a table whose digests are all re-signed (correctness review C3): 28
// entries of 8-byte names fill a page, so 70 entries take three.
void cross_page() {
	const auto table = table_of(70);
	const auto encoded = bytes_of(l::encode_table(table));
	const auto [pages, begin] = layout_of(encoded);
	check(pages == 3 && entry_at(encoded, name_of(28)) == begin + l::table_page_bytes, "three pages");
	check(l::PagedTable(resign(encoded)).decode() == table, "a re-signed whole table reads whole");
	const auto rename = [](std::vector<char> &bytes, std::size_t at, const std::string &name) {
		std::copy(name.begin(), name.end(), bytes.begin() + std::ptrdiff_t(at));
	};
	{ // Order across pages: page 1 starts with page 0's last name, a duplicate name.
		auto bytes = encoded;
		rename(bytes, entry_at(bytes, name_of(28)) + 1, name_of(27));
		const l::PagedTable paged(resign(std::move(bytes)));
		check(paged.find(name_of(27)) == table.entries[27], "page 0 keeps answering");
		check(refuses([&] { paged.find(name_of(29)); }), "a page starting at the previous page's last name refuses");
		check(refuses([&] { paged.decode(); }), "a duplicate name across pages refuses the whole read");
	}
	{ // Page bytes: a body a page and a byte short of its page count refuses at the read.
		auto bytes = encoded;
		bytes.resize(begin + l::table_page_bytes * (pages - 1) - 1);
		check(refuses([&] { l::PagedTable{resign(std::move(bytes))}; }), "a body shorter than its pages refuses");
	}
	{ // Index order: two index records' last names swapped.
		auto bytes = encoded;
		rename(bytes, 72 + 33, name_of(55));
		rename(bytes, 72 + 33 + 8 + 33, name_of(27));
		check(refuses([&] { l::PagedTable{resign(std::move(bytes))}; }), "an index out of order refuses");
	}
	{ // Entry count: the header counts one entry fewer than the pages hold.
		auto bytes = encoded;
		bytes[15] = 69;
		const l::PagedTable paged(resign(std::move(bytes)));
		check(paged.find(name_of(69)) == table.entries[69], "lookups do not count entries");
		check(refuses([&] { paged.decode(); }), "a wrong entry count refuses the whole read");
	}
	{ // Slots: the header's `slots` above the highest slot plus one.
		auto bytes = encoded;
		bytes[67] = 71;
		check(refuses([&] { l::PagedTable(resign(std::move(bytes))).decode(); }), "slots above max + 1 refuses");
	}
	{ // An entry's slot at `slots`.
		auto bytes = encoded;
		const auto at = entry_at(bytes, name_of(5)) + 1 + 8 + 16 + 16 + 8;
		bytes[at + 3] = 70;
		const l::PagedTable paged(resign(std::move(bytes)));
		check(refuses([&] { paged.find(name_of(5)); }), "an entry's slot at `slots` refuses its page");
	}
	{ // Page end: page 0's index record names an earlier last entry than the page holds.
		auto bytes = encoded;
		rename(bytes, 72 + 33, name_of(26));
		const l::PagedTable paged(resign(std::move(bytes)));
		check(refuses([&] { paged.find(name_of(0)); }), "a page whose last entry is not the index's refuses");
	}
	{ // Page padding: a nonzero byte after page 0's zero padding starts.
		auto bytes = encoded;
		bytes[begin + l::table_page_bytes - 1] = 1;
		const l::PagedTable paged(resign(std::move(bytes)));
		check(refuses([&] { paged.find(name_of(0)); }), "a nonzero byte in a page's padding refuses");
	}
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
	page_index();
	shared_prefixes();
	mutations();
	cross_page();
	std::puts("table pages: ok");
}
