// Executable model of the usage design (docs/design/observability.md D2, D3), run under TSan by
// usage_claim_model.sh: segment-bitmap marks with the fold across publishes, the row claim on the
// sampled path, and the sampling countdown. Each scenario forces one interleaving or checks one
// claim of the design; the program exits 0 when every outcome matches.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr std::uint32_t none = UINT32_MAX, overflow = UINT32_MAX - 1;
constexpr std::size_t cells = 9, spare_cells = 64;
enum Cell { Published, Missing = 7 };

enum class Window { SawEmpty, RowWritten, NamePublished, Marked };
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

// ---- Rows: sampled counts by name (D3, Rows) ----
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
	std::array<std::atomic<std::uint32_t>, spare_cells> spares{};
	std::atomic<bool> overflowed{false};
	std::array<std::atomic<std::uint64_t>, cells> overflow_samples{};
	explicit Table(std::uint32_t n) : slots(n), rows(new Row[n]), names(power_of_two(2 * std::size_t(n))) {}

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
	// Finds the published row by name, or claims one. Never waits on another thread.
	std::uint32_t find(std::string_view name) {
		const auto h = hash_of(name);
		const auto mask = names.size() - 1;
		std::uint32_t mine = none;
		for (std::size_t i = h & mask;; i = (i + 1) & mask) {
			auto v = names[i].load(std::memory_order_acquire);
			if (!v) {
				window(Window::SawEmpty);
				if (mine == none) {
					mine = allocate();
					if (mine == overflow) {
						// Full: the slot may have been published since it was seen empty (R5).
						v = names[i].load(std::memory_order_acquire);
						if (!v) {
							if (!overflowed.load(std::memory_order_relaxed))
								overflowed.store(true, std::memory_order_release);
							return overflow;
						}
						mine = none;
					} else {
						auto &row = rows[mine];
						row.hash = h;
						row.size = std::uint8_t(name.size());
						std::memcpy(row.name, name.data(), name.size());
						window(Window::RowWritten);
					}
				}
				if (!v && names[i].compare_exchange_strong(v, mine + 1, std::memory_order_release,
				                                           std::memory_order_acquire)) {
					window(Window::NamePublished);
					return mine;
				}
			}
			const auto &row = rows[v - 1];
			if (row.hash == h && row.text() == name) {
				if (mine != none)
					release(mine);
				return v - 1;
			}
		}
	}
	void sample(std::string_view name, Cell cell) {
		const auto row = find(name);
		(row == overflow ? overflow_samples[cell] : rows[row].samples[cell]).fetch_add(1, std::memory_order_relaxed);
	}
	std::size_t rows_for(std::string_view name) const {
		std::size_t n = 0;
		for (const auto &slot : names)
			if (const auto v = slot.load(std::memory_order_acquire))
				n += rows[v - 1].text() == name;
		return n;
	}
	std::size_t published() const {
		std::size_t n = 0;
		for (const auto &slot : names)
			n += slot.load(std::memory_order_acquire) != 0;
		return n;
	}
};

// ---- Marks: one bit per 8 bytes of a generation's segments (D3, Marks) ----
struct Generation {
	std::map<std::string, std::size_t, std::less<>> bodies; // key -> body offset (the trie)
	std::vector<std::atomic<std::uint64_t>> bits;
	std::atomic<int> pins{0};
	std::size_t folded = 0; // keys walked by the fold so far; the freer's
	explicit Generation(const std::vector<std::string> &keys, std::size_t stride = 16)
	    : bits(keys.size() * stride / 512 + 1) {
		std::size_t offset = 0;
		for (const auto &key : keys)
			bodies[key] = (offset += stride);
	}
	const std::size_t *find(std::string_view key) const {
		const auto at = bodies.find(key);
		return at == bodies.end() ? nullptr : &at->second;
	}
	// A warm hit: one load; the generation's first read of the key: one fetch_or.
	bool mark(std::size_t offset) {
		auto &word = bits[offset / 8 / 64];
		const auto bit = std::uint64_t(1) << (offset / 8 % 64);
		if (word.load(std::memory_order_relaxed) & bit)
			return false;
		word.fetch_or(bit, std::memory_order_relaxed);
		return true;
	}
	bool marked(std::size_t offset) const {
		return bits[offset / 8 / 64].load(std::memory_order_relaxed) >> (offset / 8 % 64) & 1;
	}
};

// The handle with one namespace: its installed generation, retired generations (still pinned or
// not yet folded), at most one pending fold, and the names carried past their generation (D3, Fold).
struct Handle {
	std::mutex refreshing; // the core's `refreshing` flag; it also stands in for the pin's view load
	std::shared_ptr<Generation> installed;
	std::vector<std::shared_ptr<Generation>> retired;
	std::vector<std::shared_ptr<Generation>> pending;
	std::set<std::string, std::less<>> carried;
	std::size_t budget = 4096;

	// A lookup: pin the installed generation, answer, mark, unpin. True when the key was found.
	bool read(const std::string &key) {
		std::shared_ptr<Generation> g;
		{
			std::lock_guard lock(refreshing);
			g = installed;
			g->pins.fetch_add(1);
		}
		const auto *offset = g->find(key);
		if (offset && g->mark(*offset))
			window(Window::Marked);
		g->pins.fetch_sub(1, std::memory_order_release);
		return offset != nullptr;
	}
	// Folds `old` into the installed generation, at most `limit` marked keys; true when done.
	bool fold(Generation &old, std::size_t limit) {
		std::size_t done = 0, at = 0;
		for (const auto &[key, offset] : old.bodies) {
			if (at++ < old.folded)
				continue;
			if (done == limit)
				return false;
			++old.folded;
			if (!old.marked(offset))
				continue;
			++done;
			if (const auto *target = installed->find(key))
				installed->mark(*target);
			else
				carried.emplace(key);
		}
		return true;
	}
	// The freer, on the refresh path: the pending fold first, then retired generations that no
	// reader pins, within the budget.
	void free_retired(std::size_t limit) {
		for (auto it = pending.begin(); it != pending.end();)
			if (fold(**it, limit))
				it = pending.erase(it);
			else
				return;
		for (auto it = retired.begin(); it != retired.end();) {
			if ((*it)->pins.load(std::memory_order_acquire)) {
				++it;
				continue;
			}
			if (!fold(**it, limit))
				pending.push_back(*it);
			it = retired.erase(it);
		}
	}
	void publish(std::shared_ptr<Generation> next) {
		std::lock_guard lock(refreshing);
		// At most one pending fold: an older one finishes now, into the generation it was folding into.
		for (auto &p : pending)
			fold(*p, SIZE_MAX);
		pending.clear();
		retired.push_back(std::exchange(installed, std::move(next)));
		free_retired(budget);
	}
	// detent_usage under `refreshing`: finishes the pending fold, then names every mark.
	std::set<std::string> usage() {
		std::lock_guard lock(refreshing);
		free_retired(SIZE_MAX);
		std::set<std::string> names(carried.begin(), carried.end());
		for (const auto &[key, offset] : installed->bodies)
			if (installed->marked(offset))
				names.insert(key);
		for (const auto &g : retired) // still pinned: reported from their own marks, folded later
			for (const auto &[key, offset] : g->bodies)
				if (g->marked(offset))
					names.insert(key);
		return names;
	}
};

std::vector<std::string> keys(int from, int to) {
	std::vector<std::string> out;
	for (int k = from; k != to; ++k)
		out.push_back("ns:k" + std::to_string(k));
	return out;
}

std::atomic<int> step{0};
void wait_for(int s) {
	while (step.load() < s)
		std::this_thread::yield();
}
void park_first_at(Window at) {
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

// A publish that reorders and deletes: every key read before it is still reported, the kept keys
// through the new generation's marks and the deleted one carried by name.
bool publish_reorder_delete() {
	Handle h;
	h.installed = std::make_shared<Generation>(keys(0, 100));
	for (const auto *key : {"ns:k3", "ns:k50", "ns:k99"})
		h.read(key);
	auto reordered = keys(0, 99); // k99 deleted
	std::reverse(reordered.begin(), reordered.end());
	h.publish(std::make_shared<Generation>(reordered, 24));
	const auto names = h.usage();
	return names == std::set<std::string>{"ns:k3", "ns:k50", "ns:k99"} && h.carried.count("ns:k99") == 1 &&
	       h.installed->marked(*h.installed->find("ns:k3")) && h.retired.empty() && h.pending.empty();
}

// A reader pinned on the old generation marks a key after the publish: the fold waits for the
// unpin, so the late mark is reported meanwhile and folded after.
bool late_mark_on_retired() {
	step = 0;
	Handle h;
	h.installed = std::make_shared<Generation>(keys(0, 10));
	park_first_at(Window::Marked);
	std::thread reader([&] { h.read("ns:k7"); }); // marks k7, parks before its unpin
	wait_for(1);
	h.publish(std::make_shared<Generation>(keys(0, 10))); // the old one is pinned: not folded
	const bool held = h.retired.size() == 1 && !h.installed->marked(*h.installed->find("ns:k7"));
	const bool reported = h.usage().count("ns:k7") == 1;
	step = 2;
	reader.join();
	hook = nullptr;
	h.usage(); // unpinned now: folds
	return held && reported && h.retired.empty() && h.installed->marked(*h.installed->find("ns:k7"));
}

// The budget: a fold larger than it stays pending and is reported; a second publish finishes the
// older pending fold first; detent_usage finishes the rest.
bool budget_and_pending() {
	Handle h;
	h.budget = 4;
	h.installed = std::make_shared<Generation>(keys(0, 64));
	for (int k = 0; k != 10; ++k)
		h.read("ns:k" + std::to_string(k));
	h.publish(std::make_shared<Generation>(keys(0, 64)));
	const bool pending_after_first = h.pending.size() == 1;
	h.publish(std::make_shared<Generation>(keys(5, 64))); // k0-k4 deleted
	const auto names = h.usage();
	bool all = names.size() == 10;
	for (int k = 0; k != 10; ++k)
		all &= names.count("ns:k" + std::to_string(k)) == 1;
	return pending_after_first && all && h.pending.empty() && h.carried.size() == 5;
}

// Stress (TSan): 8 readers mark random keys while a publisher replaces the generation 50 times with
// reordered sets that delete keys; at the end the report is exactly the keys read OK.
bool stress() {
	Handle h;
	h.budget = 8;
	h.installed = std::make_shared<Generation>(keys(0, 512));
	std::atomic<bool> stop{false};
	std::vector<std::set<std::string>> read(8);
	std::vector<std::thread> pool;
	for (int t = 0; t != 8; ++t)
		pool.emplace_back([&, t] {
			std::uint64_t x = 0x9e3779b97f4a7c15ull * std::uint64_t(t + 1);
			while (!stop.load(std::memory_order_relaxed)) {
				x ^= x << 13, x ^= x >> 7, x ^= x << 17;
				const auto key = "ns:k" + std::to_string(x % 512);
				if (h.read(key))
					read[std::size_t(t)].insert(key);
			}
		});
	for (int p = 1; p != 51; ++p) {
		auto next = keys(p % 7, 512);
		std::rotate(next.begin(), next.begin() + p, next.end());
		h.publish(std::make_shared<Generation>(next, std::size_t(16 + 8 * (p % 3))));
		std::this_thread::sleep_for(std::chrono::microseconds(200));
	}
	stop = true;
	for (auto &thread : pool)
		thread.join();
	std::set<std::string> expected;
	for (const auto &s : read)
		expected.insert(s.begin(), s.end());
	return h.usage() == expected && h.pending.empty();
}

// Rows: two claimers of one name, the first parked mid-claim: one row and no wait (F2).
bool two_claimers(Window at) {
	step = 0;
	Table table(8);
	park_first_at(at);
	std::thread one([&] { table.sample("ns:k", Published); });
	wait_for(1);
	std::atomic<bool> done{false};
	std::thread two([&] {
		table.sample("ns:k", Published);
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
	table.sample("ns:other", Published); // takes the loser's spare, if any
	return progressed && table.rows_for("ns:k") == 1 && table.next.load() == 2;
}

bool equal_hashes() {
	force_equal_hashes = true;
	Table table(8);
	table.sample("ns:a", Published);
	table.sample("ns:b", Published);
	table.sample("ns:a", Published);
	const bool ok = table.rows_for("ns:a") == 1 && table.rows_for("ns:b") == 1 && table.published() == 2;
	force_equal_hashes = false;
	return ok;
}

// R5: the table's last row is taken while another claimer of the same name, which saw the slot
// empty, finds the table full: it re-examines the slot, takes the row and sets no flag.
bool boundary() {
	step = 0;
	Table table(1);
	park_first_at(Window::SawEmpty);
	std::thread second([&] { table.sample("ns:k", Published); }); // sees the empty slot, parks
	wait_for(1);
	table.sample("ns:k", Published); // takes the last row and publishes it
	step = 2;
	second.join();
	hook = nullptr;
	return table.rows_for("ns:k") == 1 && !table.overflowed.load() && table.rows[0].samples[Published].load() == 2;
}

// Past usage_slots the counts overflow, but every key read stays reported through the marks.
bool overflow_counts() {
	Table table(4);
	Handle h;
	h.installed = std::make_shared<Generation>(keys(0, 6));
	for (int k = 0; k != 6; ++k) {
		const auto key = "ns:k" + std::to_string(k);
		h.read(key);
		table.sample(key, Published);
	}
	return h.usage().size() == 6 && table.published() == 4 && table.overflowed.load() &&
	       table.overflow_samples[Published].load() == 2;
}

// ---- Sampling (D2) ----
struct Stripe {
	std::uint64_t state, countdown;
	explicit Stripe(std::uint64_t seed) : state(seed | 1), countdown(0) { countdown = 1 + next() % 127; }
	std::uint64_t next() {
		state ^= state << 13, state ^= state >> 7, state ^= state << 17;
		return state;
	}
	bool tick(bool fixed) {
		if (--countdown)
			return false;
		countdown = fixed ? 64 : 1 + next() % 127;
		return true;
	}
};
// R4: a 1,024-name cycle on one stripe: names sampled in the first 64 cycles, and the cycles until
// every name is sampled (0: not within `limit`).
std::pair<std::size_t, std::size_t> cycle(std::uint64_t seed, bool fixed, std::size_t limit) {
	Stripe stripe(seed);
	std::vector<bool> seen(1024);
	std::size_t distinct = 0, at64 = 0;
	for (std::size_t c = 0; c != limit; ++c) {
		for (std::size_t n = 0; n != 1024; ++n)
			if (stripe.tick(fixed) && !seen[n])
				seen[n] = true, ++distinct;
		if (c == 63)
			at64 = distinct;
		if (distinct == 1024)
			return {c < 63 ? 1024 : at64, c + 1};
	}
	return {at64, 0};
}
bool cycle_coverage() {
	std::size_t worst64 = 1024, worst_full = 0;
	bool all_full = true;
	for (std::uint64_t seed = 1; seed != 101; ++seed) {
		const auto [at64, full] = cycle(seed * 0x2545f4914f6cdd1dull, false, 1000);
		worst64 = std::min(worst64, at64);
		all_full &= full != 0;
		worst_full = std::max(worst_full, full);
	}
	const auto [fixed64, fixed_full] = cycle(12345, true, 1000);
	std::printf("      cycle: over 100 seeds at least %zu of 1,024 names in 64 cycles, all within %zu cycles; "
	            "fixed period 64: %zu names, %s\n",
	            worst64, worst_full, fixed64, fixed_full ? "then all" : "never all");
	return worst64 >= 512 && all_full && worst_full <= 1000 && fixed64 == 16 && fixed_full == 0;
}
// R6: the first lookup of 10,000 fresh handles, each seeded from its own address and the clock.
bool fresh_handles() {
	std::size_t sampled = 0;
	const auto clock = std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
	for (std::uint64_t h = 0; h != 10000; ++h) {
		Stripe stripe((clock + h * 0x9e3779b97f4a7c15ull) * 0xbf58476d1ce4e5b9ull);
		sampled += stripe.tick(false);
	}
	std::printf("      fresh: %zu of 10,000 first lookups sampled (expected 78.7)\n", sampled);
	return sampled >= 30 && sampled <= 160;
}
} // namespace

int main() {
	bool ok = true;
	ok &= check(publish_reorder_delete(), "fold: a reordering, deleting publish keeps every key read (kept by mark, deleted by name)");
	ok &= check(late_mark_on_retired(), "fold: a mark on a pinned, retired generation is reported, then folded after the unpin");
	ok &= check(budget_and_pending(), "fold: past the budget the fold is pending and reported; the next publish finishes it first");
	ok &= check(stress(), "fold stress: 8 readers, 50 reordering, deleting publishes: the report is exactly the keys read");
	ok &= check(two_claimers(Window::RowWritten), "rows: two claimers, one parked with its row unpublished: one row, no wait, spare reused");
	ok &= check(two_claimers(Window::NamePublished), "rows: two claimers, one parked after publishing: one row, no wait");
	ok &= check(equal_hashes(), "rows: equal hashes, two names, two rows");
	ok &= check(boundary(), "rows R5: the last row taken under a claimer that saw it empty: one row, no flag");
	ok &= check(overflow_counts(), "rows: past usage_slots the counts overflow; every key read is still reported");
	ok &= check(cycle_coverage(), "R4 cycle: the random reload covers 1,024 names; a fixed period of 64 samples 16");
	ok &= check(fresh_handles(), "R6 fresh handles: the first lookup is sampled about 1 time in 127");
	std::printf("%s\n", ok ? "all outcomes as designed" : "UNEXPECTED outcome");
	return ok ? 0 : 1;
}
