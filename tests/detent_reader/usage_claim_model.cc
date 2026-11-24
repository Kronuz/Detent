// Executable model of the usage design (docs/design/observability.md D2, D3), run under TSan by
// usage_claim_model.sh: segment-bitmap marks exact per generation (no fold across publishes), the
// walk that names them, the row claim on the sampled path, and the sampling countdown. Each scenario forces one interleaving or checks one
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

enum class Window { SawEmpty, RowWritten, NamePublished, Marked, Refreshing };
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

// ---- Marks: one bit per 8 bytes of a generation's segments, exact per generation (D3, Marks) ----
// A generation is a two-level trie when `deep`: the root page holds branch nodes only (no body, so no
// mark), and each leaf page holds 64 bodies. A lookup verifies the pages on its path.
struct Page {
	std::atomic<bool> verified{false};
	std::vector<std::size_t> children;
	std::vector<std::pair<std::string, std::size_t>> bodies; // key, body offset
};
std::atomic<int> live_generations{0};
std::atomic<std::uint64_t> generation_ids{0};
struct Generation {
	const std::uint64_t id = generation_ids.fetch_add(1) + 1;
	std::vector<std::unique_ptr<Page>> pages; // pages[0] is the root
	std::map<std::string, std::pair<std::size_t, std::size_t>, std::less<>> index; // key -> page, offset
	std::vector<std::atomic<std::uint64_t>> bits;
	std::atomic<int> pins{0};
	explicit Generation(const std::vector<std::string> &keys, std::size_t stride = 16, bool deep = false)
	    : bits(keys.size() * stride / 512 + 1) {
		live_generations.fetch_add(1);
		pages.push_back(std::make_unique<Page>());
		std::size_t offset = 0;
		for (std::size_t i = 0; i != keys.size(); ++i) {
			std::size_t page = 0;
			if (deep) {
				if (i % 64 == 0) {
					pages[0]->children.push_back(pages.size());
					pages.push_back(std::make_unique<Page>());
				}
				page = pages.size() - 1;
			}
			pages[page]->bodies.emplace_back(keys[i], offset += stride);
			index[keys[i]] = {page, offset};
		}
	}
	~Generation() { live_generations.fetch_sub(1); }
	// The answer: verifies the root and the body's page, returns the body's offset.
	const std::size_t *find(std::string_view key) const {
		pages[0]->verified.store(true, std::memory_order_relaxed);
		const auto at = index.find(key);
		if (at == index.end())
			return nullptr;
		pages[at->second.first]->verified.store(true, std::memory_order_relaxed);
		return &at->second.second;
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
	// detent_usage's walk: enter each verified page from the root, name the marked bodies (T3). With
	// `by_marks` it enters only pages holding a mark, the walk round 3 specified.
	std::set<std::string> walk(bool by_marks = false) const {
		std::set<std::string> names;
		std::vector<std::size_t> stack{0};
		while (!stack.empty()) {
			const auto &page = *pages[stack.back()];
			stack.pop_back();
			bool any = false;
			for (const auto &[key, offset] : page.bodies)
				any |= marked(offset);
			if (by_marks ? !any : !page.verified.load(std::memory_order_relaxed))
				continue;
			for (const auto &[key, offset] : page.bodies)
				if (marked(offset))
					names.insert(key);
			stack.insert(stack.end(), page.children.begin(), page.children.end());
		}
		return names;
	}
	void clear() {
		for (auto &word : bits)
			word.store(0, std::memory_order_relaxed);
	}
};

// The handle with one namespace: the installed generation and the retired ones a reader still pins.
// `view` stands for the pin's load of the installed view; `refreshing` is held across a refresh.
struct Handle {
	std::mutex refreshing, view;
	std::shared_ptr<Generation> installed;
	std::vector<std::shared_ptr<Generation>> retired;
	std::size_t freer_work = 0; // successor lookups and allocations the freer makes: none

	std::shared_ptr<Generation> pin() {
		std::lock_guard lock(view);
		installed->pins.fetch_add(1);
		return installed;
	}
	// A lookup: pin, answer, mark, unpin. Returns the answering generation's id, or 0 when missing.
	std::uint64_t read(const std::string &key) {
		const auto g = pin();
		const auto *offset = g->find(key);
		if (offset && g->mark(*offset))
			window(Window::Marked);
		g->pins.fetch_sub(1, std::memory_order_release);
		return offset ? g->id : 0;
	}
	// The freer frees what no reader pins, and does nothing else (no fold).
	void free_retired() {
		std::erase_if(retired, [](const auto &g) { return g->pins.load(std::memory_order_acquire) == 0; });
	}
	void publish(std::shared_ptr<Generation> next) {
		std::lock_guard lock(refreshing);
		window(Window::Refreshing);
		{
			std::lock_guard pinning(view);
			retired.push_back(std::exchange(installed, std::move(next)));
		}
		free_retired();
	}
	// detent_usage: pins the installed view as a lookup does, walks, unpins; never takes `refreshing`.
	std::set<std::string> usage() {
		const auto g = pin();
		auto names = g->walk();
		g->pins.fetch_sub(1, std::memory_order_release);
		return names;
	}
	// The fork child's handler: every ring generation's marks zeroed, pins zeroed (core.cc:278-282).
	void child_after_fork(bool clear_marks = true) {
		for (auto *g : {&installed})
			if (clear_marks)
				(*g)->clear();
		for (auto &g : retired) {
			if (clear_marks)
				g->clear();
			g->pins.store(0);
		}
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

// D161: a publish that reorders and deletes starts a record of its own: nothing is carried, the
// freer frees the old generation at once and makes no lookup or allocation (T1, T2, T10).
bool publish_resets() {
	Handle h;
	h.installed = std::make_shared<Generation>(keys(0, 100));
	for (const auto *key : {"ns:k3", "ns:k50", "ns:k99"})
		h.read(key);
	const bool before = h.usage() == std::set<std::string>{"ns:k3", "ns:k50", "ns:k99"};
	auto reordered = keys(0, 99); // k99 deleted
	std::reverse(reordered.begin(), reordered.end());
	h.publish(std::make_shared<Generation>(reordered, 24));
	const bool reset = h.usage().empty() && h.retired.empty() && h.freer_work == 0 && live_generations.load() == 1;
	h.read("ns:k3");
	return before && reset && h.usage() == std::set<std::string>{"ns:k3"};
}

// A reader pinned on the old generation marks a key after the publish: the mark stays in the retired
// generation, is never reported, and goes with it when the freer frees it (T1-T4 have no subject).
bool retired_marks_unreported() {
	step = 0;
	Handle h;
	h.installed = std::make_shared<Generation>(keys(0, 10));
	park_first_at(Window::Marked);
	std::thread reader([&] { h.read("ns:k7"); }); // marks k7 in generation 1, parks before its unpin
	wait_for(1);
	h.publish(std::make_shared<Generation>(keys(0, 10))); // generation 1 is pinned: kept, not freed
	const bool held = h.retired.size() == 1 && h.usage().empty();
	step = 2;
	reader.join();
	hook = nullptr;
	h.publish(std::make_shared<Generation>(keys(0, 10))); // frees both retired generations
	return held && h.retired.empty() && h.usage().empty() && h.freer_work == 0 && live_generations.load() == 1;
}

// Stress (TSan): 8 readers mark random keys while a publisher replaces the generation 50 times and a
// reporter walks; at the end the report is exactly the keys the final generation answered.
bool stress() {
	Handle h;
	h.installed = std::make_shared<Generation>(keys(0, 512), 16, true);
	std::atomic<bool> stop{false};
	std::vector<std::vector<std::pair<std::uint64_t, std::string>>> read(8);
	std::vector<std::thread> pool;
	for (int t = 0; t != 8; ++t)
		pool.emplace_back([&, t] {
			std::uint64_t x = 0x9e3779b97f4a7c15ull * std::uint64_t(t + 1);
			while (!stop.load(std::memory_order_relaxed)) {
				x ^= x << 13, x ^= x >> 7, x ^= x << 17;
				auto key = "ns:k" + std::to_string(x % 512);
				if (const auto g = h.read(key))
					read[std::size_t(t)].emplace_back(g, std::move(key));
			}
		});
	std::thread reporter([&] {
		while (!stop.load(std::memory_order_relaxed))
			h.usage();
	});
	for (int p = 1; p != 51; ++p) {
		auto next = keys(p % 7, 512);
		std::rotate(next.begin(), next.begin() + p, next.end());
		h.publish(std::make_shared<Generation>(next, std::size_t(16 + 8 * (p % 3)), true));
		std::this_thread::sleep_for(std::chrono::microseconds(200));
	}
	stop = true;
	for (auto &thread : pool)
		thread.join();
	reporter.join();
	const auto final = h.installed->id;
	std::set<std::string> expected;
	for (const auto &reads : read)
		for (const auto &[g, key] : reads)
			if (g == final)
				expected.insert(key);
	return !expected.empty() && h.usage() == expected && h.freer_work == 0;
}

// T3: in a deep trie the root page holds no body and so no mark. The walk that enters verified pages
// names every key read; the round-3 walk, entering only pages with a mark, names none.
bool walk_deep() {
	Handle h;
	h.installed = std::make_shared<Generation>(keys(0, 1024), 16, true);
	std::set<std::string> expected;
	for (int k = 0; k < 1024; k += 37)
		h.read(expected.emplace("ns:k" + std::to_string(k)).first->c_str());
	return h.usage() == expected && h.installed->walk(true).empty();
}

// T4: the fork child zeroes every ring generation's marks, including one a parked reader pins; the
// child reports none of the parent's reads, then its own. Without the clear it reports the parent's.
bool fork_child(bool clear_marks) {
	step = 0;
	Handle h;
	h.installed = std::make_shared<Generation>(keys(0, 10));
	h.read("ns:k1");
	park_first_at(Window::Marked);
	std::thread parked([&] { h.read("ns:k2"); });
	wait_for(1);
	h.publish(std::make_shared<Generation>(keys(0, 10))); // generation 1 retired, pinned
	h.read("ns:k3");
	h.child_after_fork(clear_marks); // the child's view of the ring
	const bool none = h.usage().empty();
	h.publish(std::make_shared<Generation>(keys(0, 10))); // the child frees the retired ones
	step = 2;
	parked.join();
	hook = nullptr;
	h.read("ns:k4");
	return none && h.usage() == std::set<std::string>{"ns:k4"};
}

// T7: detent_usage neither waits for nor takes `refreshing`: with a refresher parked inside it,
// detent_usage returns at once and reports the installed generation.
bool usage_no_wait() {
	step = 0;
	Handle h;
	h.installed = std::make_shared<Generation>(keys(0, 10));
	h.read("ns:k5");
	park_first_at(Window::Refreshing);
	std::thread refresher([&] { h.publish(std::make_shared<Generation>(keys(0, 10))); });
	wait_for(1);
	std::atomic<bool> done{false};
	std::set<std::string> names;
	std::thread reporter([&] {
		names = h.usage();
		done = true;
	});
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!done && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	const bool returned = done;
	step = 2;
	refresher.join();
	reporter.join();
	hook = nullptr;
	return returned && names == std::set<std::string>{"ns:k5"};
}

// T11: closing with a retired, unfreed generation that holds marks frees every generation.
bool close_retired() {
	{
		step = 0;
		Handle h;
		h.installed = std::make_shared<Generation>(keys(0, 10));
		h.read("ns:k1");
		auto held = h.pin(); // a pin that outlives the publish, released before close
		h.publish(std::make_shared<Generation>(keys(0, 10)));
		held->pins.fetch_sub(1);
		held.reset();
		if (h.retired.size() != 1 || live_generations.load() != 2)
			return false;
	}
	return live_generations.load() == 0;
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

// Past usage_slots the counts overflow, but every key read stays reported through the marks (R2).
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
	ok &= check(publish_resets(), "D161: a reordering, deleting publish starts an empty record; the freer frees at once, no lookup or allocation");
	ok &= check(retired_marks_unreported(), "T1-T4: a late mark on a pinned, retired generation is never reported and leaves with it");
	ok &= check(stress(), "stress: 8 readers, a reporter, 50 reordering, deleting publishes: the report is the final generation's reads");
	ok &= check(walk_deep(), "T3: the walk enters verified pages and names every key read; entering marked pages names none");
	ok &= check(fork_child(true), "T4: the fork child clears every ring generation's marks and reports only its own reads");
	ok &= check(!fork_child(false), "T4: without the clear the child reports the parent's reads (the check can fail)");
	ok &= check(usage_no_wait(), "T7: detent_usage returns while a refresher holds refreshing, with the installed generation");
	ok &= check(close_retired(), "T11: close with a retired, unfreed, marked generation frees every generation");
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
