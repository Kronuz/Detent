// Executable model of the usage claim protocol (docs/design/observability.md P2), run under TSan
// by usage_claim_model.sh. Each scenario forces one interleaving the design review found
// (observability-core-design.md F1-F3) or one the protocol adds; the program exits 0 when every
// outcome matches the design.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
constexpr std::uint32_t unstored = UINT32_MAX, overflow = UINT32_MAX - 1;
constexpr std::size_t cells = 9, spare_cells = 64;
enum Cell { Published, Missing = 7 };

// Windows a test parks a thread at (testing::Window in the core).
enum class Window { RowWritten, NamePublished, AddressClaimed };
std::function<void(Window)> hook;
void window(Window at) {
	if (hook)
		hook(at);
}
bool force_equal_hashes = false;
std::uint64_t hash_of(std::string_view name) {
	return force_equal_hashes ? 42 : std::hash<std::string_view>{}(name);
}
std::size_t power_of_two(std::size_t n) {
	std::size_t p = 1;
	while (p < n)
		p <<= 1;
	return p;
}

// A row is written privately, then published once by the name set's compare-and-swap; it never
// changes name after.
struct Row {
	std::uint64_t hash = 0;
	std::uint8_t size = 0;
	char name[200]{};
	std::array<std::atomic<std::uint64_t>, cells> samples{};
	std::string_view text() const { return {name, size}; }
};
struct Table {
	std::uint32_t slots;
	std::unique_ptr<Row[]> rows;
	std::vector<std::atomic<std::uint32_t>> names; // 0 empty, else row + 1; insert-only
	std::atomic<std::uint64_t> next{0};
	std::array<std::atomic<std::uint32_t>, spare_cells> spares{}; // 0 empty, else row + 1
	std::atomic<bool> overflowed{false};
	std::array<std::atomic<std::uint64_t>, cells> overflow_samples{};
	std::atomic<std::uint64_t> missing_total{0}; // the handle's exact status counter
	explicit Table(std::uint32_t n) : slots(n), rows(new Row[n]), names(power_of_two(2 * std::size_t(n))) {}

	void set_overflowed() {
		if (!overflowed.load(std::memory_order_relaxed))
			overflowed.store(true, std::memory_order_release);
	}
	std::uint32_t allocate() {
		for (auto &spare : spares)
			if (spare.load(std::memory_order_relaxed))
				if (const auto r = spare.exchange(0, std::memory_order_acquire))
					return r - 1;
		if (next.load(std::memory_order_relaxed) < slots)
			if (const auto n = next.fetch_add(1, std::memory_order_relaxed); n < slots)
				return std::uint32_t(n);
		return overflow;
	}
	void release(std::uint32_t row) {
		for (auto &spare : spares) {
			std::uint32_t empty = 0;
			if (spare.compare_exchange_strong(empty, row + 1, std::memory_order_release, std::memory_order_relaxed))
				return;
		}
	}
	// Finds the published row by name; claims one when `claim` and none exists. Never waits.
	std::uint32_t find(std::string_view name, bool claim) {
		const auto h = hash_of(name);
		const auto mask = names.size() - 1;
		std::uint32_t mine = unstored;
		for (std::size_t i = h & mask;; i = (i + 1) & mask) {
			auto v = names[i].load(std::memory_order_acquire);
			if (!v) {
				if (!claim)
					return unstored;
				if (mine == unstored) {
					mine = allocate();
					if (mine == overflow) {
						set_overflowed();
						return overflow;
					}
					auto &row = rows[mine];
					row.hash = h;
					row.size = std::uint8_t(name.size());
					std::memcpy(row.name, name.data(), name.size());
					window(Window::RowWritten);
				}
				if (names[i].compare_exchange_strong(v, mine + 1, std::memory_order_release, std::memory_order_acquire)) {
					window(Window::NamePublished);
					return mine;
				}
			}
			const auto &row = rows[v - 1];
			if (row.hash == h && row.text() == name) {
				if (mine != unstored)
					release(mine);
				return v - 1;
			}
		}
	}
	struct Report {
		std::vector<std::string> names;
		bool overflowed;
	};
	Report usage() const {
		Report out{{}, overflowed.load(std::memory_order_acquire)};
		for (const auto &slot : names)
			if (const auto v = slot.load(std::memory_order_acquire))
				out.names.emplace_back(rows[v - 1].text());
		return out;
	}
	std::size_t rows_for(std::string_view name) const {
		const auto report = usage();
		return std::size_t(std::count(report.names.begin(), report.names.end(), name));
	}
};

// One generation's claim index, keyed by the definition body's address.
struct Index {
	struct Slot {
		std::atomic<std::uintptr_t> address{0};
		std::atomic<std::uint32_t> row{unstored};
	};
	std::vector<Slot> slots;
	std::atomic<std::uint32_t> used{0};
	std::atomic<std::uint64_t> probes{0}; // model-only: slots examined by hits
	explicit Index(std::size_t n) : slots(power_of_two(2 * n)) {}
};

struct Lookup {
	Table &table;
	Index &index;
	// An OK lookup's mark (P2): the row is published, or the overflow flag set, before the address.
	void ok(std::uintptr_t body, std::string_view name, bool sampled) {
		const auto mask = index.slots.size() - 1;
		// Multiply-shift: the product's high bits, since aligned addresses share their low bits.
		const auto bits = unsigned(std::countr_zero(index.slots.size()));
		const auto start = bits ? std::size_t((std::uint64_t(body) * 0x9e3779b97f4a7c15ull) >> (64 - bits)) : 0;
		for (std::size_t i = start;; i = (i + 1) & mask) {
			auto &slot = index.slots[i];
			auto a = slot.address.load(std::memory_order_acquire);
			index.probes.fetch_add(1, std::memory_order_relaxed);
			if (a == body)
				return count(slot, name, sampled);
			if (a)
				continue;
			if (index.used.load(std::memory_order_relaxed) >= index.slots.size() / 2)
				return table.set_overflowed(), add(overflow, sampled);
			const auto row = table.find(name, true);
			if (slot.address.compare_exchange_strong(a, body, std::memory_order_acq_rel, std::memory_order_acquire)) {
				index.used.fetch_add(1, std::memory_order_relaxed);
				window(Window::AddressClaimed);
				slot.row.store(row, std::memory_order_release);
				return add(row, sampled);
			}
			if (a == body)
				return count(slot, name, sampled);
		}
	}
	void count(Index::Slot &slot, std::string_view name, bool sampled) {
		if (!sampled)
			return;
		auto row = slot.row.load(std::memory_order_acquire);
		if (row == unstored) // the claimer is between its address and its row store: the row exists
			row = table.find(name, true);
		add(row, sampled);
	}
	void add(std::uint32_t row, bool sampled) {
		if (!sampled)
			return;
		auto &cell = row == overflow ? table.overflow_samples[Published] : table.rows[row].samples[Published];
		cell.fetch_add(1, std::memory_order_relaxed);
	}
	// A missing lookup takes no row (F3): exact in the handle's counter, and when sampled in the
	// name's row if one exists.
	void missing(std::string_view name, bool sampled) {
		table.missing_total.fetch_add(1, std::memory_order_relaxed);
		if (!sampled)
			return;
		if (const auto row = table.find(name, false); row != unstored)
			table.rows[row].samples[Missing].fetch_add(1, std::memory_order_relaxed);
	}
};

std::atomic<int> step{0};
void wait_for(int s) {
	while (step.load() < s)
		std::this_thread::yield();
}
// Parks the first thread that reaches `at` until step 2.
void park_at(Window at) {
	auto parked = std::make_shared<std::atomic<bool>>(false);
	hook = [at, parked](Window w) {
		if (w == at && !parked->exchange(true)) {
			step.store(1);
			wait_for(2);
		}
	};
}
bool check(bool ok, const char *what) {
	std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	return ok;
}

// F1: the winner parks between its address and its row store; another lookup returns, then usage.
bool visibility(Window at) {
	step = 0;
	Table table(8);
	Index index(8);
	park_at(at);
	std::thread winner([&] { Lookup{table, index}.ok(0x1000, "ns:k", false); });
	wait_for(1);
	Lookup{table, index}.ok(0x1000, "ns:k", true); // returns while the winner is parked
	const auto report = table.usage();
	step = 2;
	winner.join();
	hook = nullptr;
	const bool visible = std::count(report.names.begin(), report.names.end(), "ns:k") == 1;
	return visible && table.rows_for("ns:k") == 1;
}

// F2: two claimers of one name (two generations); the first parks mid-claim. The second must
// finish while the first is parked, and the two must end with one row.
bool two_claimers(Window at) {
	step = 0;
	Table table(8);
	Index first(8), second(8);
	park_at(at);
	std::thread one([&] { Lookup{table, first}.ok(0x1000, "ns:k", true); });
	wait_for(1);
	std::atomic<bool> done{false};
	std::thread two([&] {
		Lookup{table, second}.ok(0x9000, "ns:k", true);
		done = true;
	});
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!done && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	const bool progressed = done;
	step = 2;
	one.join();
	two.join();
	hook = nullptr;
	// The loser's private row returns as a spare: the next name takes it, not a new row.
	Index third(8);
	Lookup{table, third}.ok(0x2000, "ns:other", false);
	const bool reused = at != Window::RowWritten || table.next.load() == 2;
	return progressed && table.rows_for("ns:k") == 1 && reused &&
	       table.rows[table.find("ns:k", false)].samples[Published].load() == 2;
}

// F2: equal hashes never merge two names (the core's seam forces the hash).
bool equal_hashes() {
	force_equal_hashes = true;
	Table table(8);
	Index index(8);
	Lookup{table, index}.ok(0x1000, "ns:a", true);
	Lookup{table, index}.ok(0x2000, "ns:b", true);
	Lookup{table, index}.ok(0x1000, "ns:a", true);
	const auto a = table.find("ns:a", false), b = table.find("ns:b", false);
	force_equal_hashes = false;
	return a != b && table.rows[a].samples[Published].load() == 2 && table.rows[b].samples[Published].load() == 1;
}

// F3: sampled missing names take no row, so they cannot push a defined knob into overflow.
bool spray() {
	Table table(4);
	Index index(4);
	Lookup lookup{table, index};
	for (int i = 0; i != 64; ++i)
		lookup.missing("ns:undefined" + std::to_string(i), true);
	lookup.ok(0x1000, "ns:defined", true);
	lookup.missing("ns:defined", true); // deleted later: the missing sample lands in its row
	const auto report = table.usage();
	const auto row = table.find("ns:defined", false);
	return report.names.size() == 1 && !report.overflowed && table.missing_total.load() == 65 &&
	       table.rows[row].samples[Missing].load() == 1;
}

// A full table: the knob past the bound sets the flag, its index slot holds the overflow row,
// and its later hits take one probe.
bool full_table() {
	Table table(2);
	Index index(8);
	Lookup lookup{table, index};
	lookup.ok(0x1000, "ns:a", true);
	lookup.ok(0x2000, "ns:b", true);
	lookup.ok(0x3000, "ns:c", true);
	const auto before = index.probes.load();
	for (int i = 0; i != 100; ++i)
		lookup.ok(0x3000, "ns:c", true);
	const auto report = table.usage();
	std::printf("      full_table: rows %zu overflowed %d overflow samples %llu probes %llu\n", report.names.size(),
	            int(report.overflowed), (unsigned long long)table.overflow_samples[Published].load(),
	            (unsigned long long)(index.probes.load() - before));
	return report.names.size() == 2 && report.overflowed && table.overflow_samples[Published].load() == 101 &&
	       index.probes.load() - before <= 100 + 100 / 4; // linear probing at load <= 1/2
}

// An index past half full records nothing new and says so.
bool index_half_full() {
	Table table(64);
	Index index(2); // 4 slots: claims stop at 2
	Lookup lookup{table, index};
	lookup.ok(0x1000, "ns:a", false);
	lookup.ok(0x2000, "ns:b", false);
	const bool clean = !table.usage().overflowed;
	lookup.ok(0x3000, "ns:c", false);
	return clean && table.usage().overflowed && index.used.load() == 2;
}

// Stress (TSan): 16 threads, two generations of 64 knobs, every lookup sampled: one row per name
// and exact sums.
bool stress() {
	Table table(256);
	Index g1(64), g2(64);
	constexpr int threads = 16, rounds = 200;
	std::vector<std::thread> pool;
	for (int t = 0; t != threads; ++t)
		pool.emplace_back([&, t] {
			for (int r = 0; r != rounds; ++r)
				for (int k = 0; k != 64; ++k) {
					auto &index = (r + t) % 2 ? g1 : g2;
					const auto body = std::uintptr_t(((r + t) % 2 ? 0x100000 : 0x200000) + k * 64);
					Lookup{table, index}.ok(body, "ns:k" + std::to_string(k), true);
				}
		});
	for (auto &thread : pool)
		thread.join();
	const auto report = table.usage();
	std::uint64_t total = 0;
	bool unique = report.names.size() == 64;
	for (int k = 0; k != 64; ++k) {
		const auto name = "ns:k" + std::to_string(k);
		unique &= table.rows_for(name) == 1;
		total += table.rows[table.find(name, false)].samples[Published].load();
	}
	return unique && !report.overflowed && total == std::uint64_t(threads) * rounds * 64 &&
	       table.next.load() <= 64 + spare_cells;
}
} // namespace

int main() {
	std::printf("index slot: %zu bytes, row: %zu bytes\n", sizeof(Index::Slot), sizeof(Row));
	bool ok = true;
	ok &= check(visibility(Window::AddressClaimed), "F1 visibility: winner parked after its address, a returned read shows its row");
	ok &= check(visibility(Window::RowWritten), "F1 visibility: winner parked before publishing its row, a returned read shows its row");
	ok &= check(two_claimers(Window::RowWritten), "F2 two claimers, one parked with its row unpublished: one row, no wait, spare reused");
	ok &= check(two_claimers(Window::NamePublished), "F2 two claimers, one parked after publishing: one row, no wait");
	ok &= check(two_claimers(Window::AddressClaimed), "F2 two claimers, one parked before its row store: one row, no wait");
	ok &= check(equal_hashes(), "F2 equal hashes: two names, two rows");
	ok &= check(spray(), "F3 missing names take no row: the defined knob keeps its row, no overflow");
	ok &= check(full_table(), "bound: the knob past the table sets the flag; its hits take one probe");
	ok &= check(index_half_full(), "bound: an index past half full records nothing new and sets the flag");
	ok &= check(stress(), "stress: 16 threads, two generations, one row per name, exact sums");
	std::printf("%s\n", ok ? "all outcomes as designed" : "UNEXPECTED outcome");
	return ok ? 0 : 1;
}
