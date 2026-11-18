// Review X4: a host's placement of groups on its loops, with a loop count L that may change at
// restart, any L >= 1 (D10). Placement is local to the host (Track C D16 connections are per node pair).
//   map     every L in 1..4,096: loop(slot, L) = (slot * L) >> 12 puts each of the 4,096 slots on
//           a loop below L, in contiguous ranges of floor or ceil(4,096 / L) slots
//   remap   restarts that change L (8->6, 6->12, 16->3, 3->1, 12->12) over a model of the turn
//           logs, one group in fifty lost to damage in the old generation: open recovers every
//           log of the stored generation and marks the groups recovery calls lost in the host's
//           group table (review R1), writes each group's live records into its new loop's log
//           of the next generation, syncs each, then commits by replacing the layout record; a
//           crash at every step recovers every group's records exactly once, from the old
//           generation before the commit and the new one after, and every lost group stays
//           lost, through two more restarts as well
//   remap-forget  the control: no lost marks, D10 as at Detent:1ece2ff; exits 1
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <set>
#include <string_view>
#include <vector>

namespace {
#define CHECK(c)                                                                     \
	do {                                                                             \
		if (!(c)) {                                                                  \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
			std::exit(1);                                                            \
		}                                                                            \
	} while (0)

constexpr std::uint32_t slots = 4096;

// Division-free: a slot's loop is its position in [0, 4,096) scaled to [0, L).
std::uint32_t loop_of(std::uint32_t slot, std::uint32_t L) { return static_cast<std::uint32_t>((std::uint64_t{slot} * L) >> 12); }
// The slot of an opaque group id (review X9: ids never derive from paths).
std::uint32_t slot_of(std::uint64_t id) { return static_cast<std::uint32_t>((id * 0x9E3779B97F4A7C15ull) >> 52); }

void map() {
	for (std::uint32_t L = 1; L <= slots; ++L) {
		std::vector<std::uint32_t> count(L, 0);
		std::uint32_t previous = 0;
		for (std::uint32_t s = 0; s < slots; ++s) {
			const std::uint32_t l = loop_of(s, L);
			CHECK(l < L && l >= previous);  // contiguous: the loop never decreases with the slot
			previous = l;
			++count[l];
		}
		const auto [lo, hi] = std::minmax_element(count.begin(), count.end());
		CHECK(*lo == slots / L && *hi == (slots + L - 1) / L);
	}
	std::printf("map L=1..4096 every slot on one loop, contiguous, floor/ceil(4096/L) slots per loop ok\n");
}

// The turn logs of one host, as durable state a crash leaves: per generation and loop, the
// records each group has there (synced logs only) and the groups its damage makes recovery call
// lost (review R1: journal's TurnRecovery::lost, which exists only as that log's damage); the
// layout record naming (L, generation); and the host's group table, whose lost marks every open
// writes from recovery before it serves or re-partitions.
struct record {
	std::uint64_t sequence;
	std::uint32_t bytes;
	bool operator==(const record&) const = default;
};
using group_records = std::map<std::uint64_t, std::vector<record>>;  // group id -> live records
struct turn_log {
	group_records groups;
	std::set<std::uint64_t> damaged;  // recovery calls these lost: a gap, or records in a damaged block
};
struct disk {
	std::uint32_t L = 0, generation = 0;                               // the layout record
	std::set<std::uint64_t> lost;                                      // the group table's lost marks
	std::map<std::pair<std::uint32_t, std::uint32_t>, turn_log> logs;  // (generation, loop) -> log
};
bool forget_lost = false;  // the control: D10 as at Detent:1ece2ff, no lost marks; live records only

// What an open of the stored generation calls lost: its logs' damage and the group table's marks.
std::set<std::uint64_t> lost_of(const disk& d) {
	std::set<std::uint64_t> lost = d.lost;
	for (const auto& [key, log] : d.logs) {
		if (key.first == d.generation) { lost.insert(log.damaged.begin(), log.damaged.end()); }
	}
	return lost;
}

// Opens a host configured with L over `d`, running the re-partition if L changed. `crash_after`
// counts durable steps (the group table's lost marks, a new log synced, the layout replaced, an
// old log deleted); the open stops there, as a crash would. Returns false if it crashed.
bool open(disk& d, std::uint32_t L, std::int64_t crash_after, std::uint64_t& copied) {
	std::int64_t steps = 0;
	auto step = [&] { return crash_after >= 0 && ++steps > crash_after; };
	// Recovery: the layout's generation is authoritative; a log of any other generation is a
	// re-partition that did not commit, or one whose old logs were not all deleted.
	for (auto it = d.logs.begin(); it != d.logs.end();) {
		it = it->first.first != d.generation ? d.logs.erase(it) : std::next(it);
	}
	const std::set<std::uint64_t> lost = lost_of(d);  // step 1: recover every log
	if (!forget_lost && lost != d.lost) {  // every open marks what recovery lost, then serves
		if (step()) { return false; }
		d.lost = lost;
	}
	if (d.L == L) { return true; }
	group_records live;  // a lost group has no live records
	for (const auto& [key, log] : d.logs) {
		for (const auto& [g, records] : log.groups) {
			CHECK(!live.count(g) && !lost.count(g));  // a group's records are in exactly one log
			live[g] = records;
		}
	}
	const std::uint32_t next = d.generation + 1;
	std::vector<turn_log> fresh(L);
	for (const auto& [g, records] : live) {
		fresh[loop_of(slot_of(g), L)].groups[g] = records;
		for (const auto& r : records) { copied += r.bytes; }
	}
	for (std::uint32_t i = 0; i < L; ++i) {  // step 2: write, then one barrier per new log
		if (step()) { return false; }
		d.logs[{next, i}] = fresh[i];
	}
	if (step()) { return false; }
	d.L = L;  // step 3, the commit point: the layout record names the new generation
	d.generation = next;
	for (auto it = d.logs.begin(); it != d.logs.end();) {
		if (it->first.first == next) {
			++it;
			continue;
		}
		if (step()) { return false; }
		it = d.logs.erase(it);
	}
	return true;
}

// Every group's records, once each, on the loop its slot names under the layout's L, and every
// lost group still lost: its old id would append from sequence 1 again otherwise.
bool verify(const disk& d, const group_records& expected, const std::set<std::uint64_t>& lost) {
	group_records seen;
	for (const auto& [key, log] : d.logs) {
		CHECK(key.first == d.generation && key.second < d.L);
		for (const auto& [g, records] : log.groups) {
			CHECK(loop_of(slot_of(g), d.L) == key.second);
			CHECK(seen.emplace(g, records).second);
		}
	}
	CHECK(seen == expected);
	return lost_of(d) == lost;
}

struct stored {
	disk d;
	group_records live;
	std::set<std::uint64_t> lost;
};
// A host with L loops and `groups` groups; about one in fifty is lost to damage in its log.
stored host(std::uint32_t L, std::size_t groups, std::uint64_t seed) {
	std::mt19937_64 random(seed);
	stored h;
	h.d.L = L;
	h.d.generation = 1;
	for (std::size_t n = 0; n < groups; ++n) {
		const std::uint64_t id = random();
		auto& log = h.d.logs[{1, loop_of(slot_of(id), L)}];
		if (random() % 50 == 0) {
			log.damaged.insert(id);
			h.lost.insert(id);
			continue;
		}
		std::vector<record> records;
		for (std::uint64_t k = 1 + random() % 8, q = 1 + random() % 1000; k; --k) {
			records.push_back({q++, static_cast<std::uint32_t>(64 + random() % 4033)});
		}
		h.live[id] = records;
		log.groups[id] = records;
	}
	return h;
}

// Returns false when a lost group came back not lost (only the control does that).
bool remap() {
	bool kept = true;
	for (auto [from, to] : {std::pair{8u, 6u}, {6u, 12u}, {16u, 3u}, {3u, 1u}, {12u, 12u}}) {
		const auto base = host(from, 2000, from * 131 + to);
		std::uint64_t live = 0;
		for (const auto& [g, records] : base.live) {
			for (const auto& r : records) { live += r.bytes; }
		}
		int crashes = 0, forgotten = 0;
		for (std::int64_t crash = 0;; ++crash) {  // crash after each durable step, then reopen
			disk d = base.d;
			std::uint64_t copied = 0;
			if (open(d, to, crash, copied)) { break; }
			++crashes;
			if (d.generation == base.d.generation) {
				group_records old;  // before the commit point: the old generation is intact
				for (const auto& [key, log] : d.logs) {
					if (key.first == base.d.generation) {
						for (const auto& [g, records] : log.groups) { old[g] = records; }
					}
				}
				CHECK(old == base.live);
			}
			forgotten += lost_of(d) != base.lost;  // what an open at this crash point would call lost
			copied = 0;
			CHECK(open(d, to, -1, copied));
			forgotten += !verify(d, base.live, base.lost);
			// Two more restarts, back and forth: the verdict carries through every later generation.
			CHECK(open(d, from, -1, copied));
			forgotten += !verify(d, base.live, base.lost);
			CHECK(open(d, to, -1, copied));
			forgotten += !verify(d, base.live, base.lost);
		}
		disk d = base.d;
		std::uint64_t copied = 0;
		CHECK(open(d, to, -1, copied));
		forgotten += !verify(d, base.live, base.lost);
		CHECK(copied == (from == to ? 0 : live));
		std::printf("remap%s L=%u->%u groups=2000 lost=%zu crash_points=%d live_bytes=%llu copied_bytes=%llu new_barriers=%u "
		            "lost_forgotten=%d %s\n",
			forget_lost ? "-forget" : "", from, to, base.lost.size(), crashes, static_cast<unsigned long long>(live),
			static_cast<unsigned long long>(copied), from == to ? 0 : to, forgotten, forgotten ? "FAILS" : "ok");
		kept = kept && !forgotten;
	}
	return kept;
}

}  // namespace

int main(int argc, char** argv) {
	const std::string_view only = argc > 1 ? argv[1] : "all";
	if (only == "all" || only == "map") { map(); }
	if (only == "remap-forget") {
		forget_lost = true;
		return remap() ? 0 : 1;
	}
	if (only == "all" || only == "remap") { CHECK(remap()); }
	if (only == "all") {  // the control
		forget_lost = true;  // D10 as at Detent:1ece2ff: a lost group appends again after the commit
		CHECK(!remap());
		forget_lost = false;
		std::printf("control: a re-partition that copies live records only forgets lost groups (expected) ok\n");
	}
	std::printf("placement_test: ok\n");
	return 0;
}
