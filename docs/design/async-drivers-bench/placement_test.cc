// Review X4: D10's placement with a loop count L that may change at restart, any L >= 1.
//   map     every L in 1..4,096: loop(slot, L) = (slot * L) >> 12 puts each of the 4,096 slots on
//           a loop below L, in contiguous ranges of floor or ceil(4,096 / L) slots
//   share   every pair L_a, L_b in 1..64: both ends compute the same loops for every slot from
//           (L_a, L_b) alone, and a loop of A shares slots with at most ceil(L_b / L_a) + 1 loops
//           of B, the Control connections it holds to that host
//   remap   restarts that change L (8->6, 6->12, 16->3, 3->1, 12->12) over a model of the turn
//           logs, one group in fifty lost to damage in the old generation: open recovers every
//           log of the stored generation and marks the groups recovery calls lost in the host's
//           group table (review R1), writes each group's live records into its new loop's log
//           of the next generation, syncs each, then commits by replacing the layout record; a
//           crash at every step recovers every group's records exactly once, from the old
//           generation before the commit and the new one after, and every lost group stays
//           lost, through two more restarts as well
//   remap-forget  the control: no lost marks, D10 as at Detent:1ece2ff; exits 1
//   hello   a dialer with a stale view of the peer's L (shrinks and growths): a dial to a loop
//           that no longer listens falls back to loop 0, whose hello carries L (review R2); a
//           hello whose L differs closes with l_mismatch and the dialer redials with it; every
//           group reaches its owning loop in at most 3 dials; a group-open sent to a loop that
//           does not own its slot is refused with misrouted, counted apart from unbound dials
//   mod     the rule at Detent:f48f7cb, loop = slot mod L, checked against `share`: exits 1, as
//           loop 0 of a 2-loop host shares slots with all 5 loops of a 5-loop host (bound 4)
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
bool use_mod = false;  // the control: the rule at Detent:f48f7cb

// Division-free: a slot's loop is its position in [0, 4,096) scaled to [0, L).
std::uint32_t loop_of(std::uint32_t slot, std::uint32_t L) { return use_mod ? slot % L : static_cast<std::uint32_t>((std::uint64_t{slot} * L) >> 12); }
// The slot of an opaque group id, alike on every host (review X9: ids never derive from paths).
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

// Which loops of a peer with L_b each loop of this host (L_a) shares slots with: one pass.
std::vector<std::set<std::uint32_t>> shared_loops(std::uint32_t La, std::uint32_t Lb) {
	std::vector<std::set<std::uint32_t>> out(La);
	for (std::uint32_t s = 0; s < slots; ++s) { out[loop_of(s, La)].insert(loop_of(s, Lb)); }
	return out;
}

bool share(bool exit_on_failure) {
	std::uint32_t worst_extra = 0, failures = 0;
	for (std::uint32_t La = 1; La <= 64; ++La) {
		for (std::uint32_t Lb = 1; Lb <= 64; ++Lb) {
			const std::uint32_t bound = (Lb + La - 1) / La + 1;
			const auto mine = shared_loops(La, Lb), theirs = shared_loops(Lb, La);
			for (std::uint32_t i = 0; i < La; ++i) {
				// The other end agrees: loop j of B shares slots with i exactly when i shares with j.
				for (std::uint32_t j : mine[i]) { CHECK(theirs[j].count(i)); }
				const auto n = static_cast<std::uint32_t>(mine[i].size());
				if (n > bound) {
					if (!failures) { std::printf("share L_a=%u L_b=%u loop %u shares slots with %u loops, bound %u FAILS\n", La, Lb, i, n, bound); }
					++failures;
				}
				worst_extra = std::max(worst_extra, n - std::min(n, (Lb + La - 1) / La));
			}
		}
	}
	if (failures) {
		std::printf("share rule=%s loops over the bound: %u\n", use_mod ? "mod" : "range", failures);
		if (exit_on_failure) { std::exit(1); }
		return false;
	}
	std::printf("share L_a,L_b=1..64 both ends agree; a loop shares slots with at most ceil(L_b/L_a)+1 peer loops (worst extra %u) ok\n", worst_extra);
	for (auto [La, Lb] : {std::pair{16u, 16u}, {12u, 16u}, {6u, 8u}, {24u, 32u}, {8u, 6u}}) {
		std::size_t most = 0;
		for (const auto& peers : shared_loops(La, Lb)) { most = std::max(most, peers.size()); }
		std::printf("share L_a=%u L_b=%u connections per loop per peer host at most %zu\n", La, Lb, most);
	}
	return true;
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

// The connection hello carries (base port, L, loop index); a group-open carries the group id.
// Loop j listens on base port + j only for j < L, so a dial computed from a stale, larger L may
// find no listener (review R2); loop 0 exists for every L >= 1.
enum class verdict { served, l_mismatch, misrouted, unbound };
verdict dial(std::uint32_t loop, std::uint32_t view_L, std::uint32_t actual_L) {
	if (loop >= actual_L) { return verdict::unbound; }  // refused (or another process's port: no hello)
	return view_L == actual_L ? verdict::served : verdict::l_mismatch;
}
verdict group_open(std::uint64_t id, std::uint32_t receiving_loop, std::uint32_t actual_L) {
	return loop_of(slot_of(id), actual_L) == receiving_loop ? verdict::served : verdict::misrouted;
}

struct dial_counts {
	std::size_t groups = 0, unbound = 0, l_mismatch = 0, misrouted = 0, dials = 0, most_dials = 0;
};
// One group's dial from a view of the peer's L: a refused or failed dial falls back to loop 0,
// and a hello whose L differs closes with l_mismatch and redials with the hello's L. Returns the
// loop that served the group-open.
std::uint32_t reach(std::uint64_t id, std::uint32_t view, std::uint32_t actual, dial_counts& n) {
	std::size_t dials = 0;
	std::uint32_t target = loop_of(slot_of(id), view);
	for (;;) {
		++dials;
		const verdict v = dial(target, view, actual);
		if (v == verdict::unbound) {
			++n.unbound;
			CHECK(target != 0);  // loop 0 always listens
			target = 0;
			continue;
		}
		if (v == verdict::l_mismatch) {
			++n.l_mismatch;
			view = actual;  // the hello's L; the dialer's view of the peer updates
			target = loop_of(slot_of(id), view);
			continue;
		}
		CHECK(group_open(id, target, actual) == verdict::served);
		break;
	}
	n.dials += dials;
	n.most_dials = std::max(n.most_dials, dials);
	return target;
}

void hello_checks() {
	std::mt19937_64 random(5);
	for (auto [view, actual] : {std::pair{8u, 6u}, {16u, 3u}, {16u, 12u}, {3u, 1u}, {6u, 12u}, {12u, 12u}}) {
		dial_counts n;
		for (int k = 0; k < 100000; ++k) {  // every group dialed from the stale view, as a fresh dialer would
			const std::uint64_t id = random();
			++n.groups;
			CHECK(reach(id, view, actual, n) == loop_of(slot_of(id), actual));
			// A group-open on a connection whose hello agreed, sent to a loop that does not own the slot.
			if (actual > 1) {
				const std::uint32_t wrong = (loop_of(slot_of(id), actual) + 1) % actual;
				n.misrouted += group_open(id, wrong, actual) == verdict::misrouted;
			}
		}
		if (view == actual) { CHECK(n.unbound == 0 && n.l_mismatch == 0 && n.most_dials == 1); }
		if (actual > 1) { CHECK(n.misrouted == n.groups); }
		CHECK(n.most_dials <= 3);  // the stale loop, loop 0, the owning loop
		std::printf("hello view_L=%u actual_L=%u groups=%zu unbound_first_dials=%zu (%.1f%%) l_mismatch=%zu dials=%zu most_dials_per_group=%zu "
		            "all_reach_owning_loop=1 misrouted_refused=%zu ok\n",
			view, actual, n.groups, n.unbound, 100.0 * double(n.unbound) / double(n.groups), n.l_mismatch, n.dials, n.most_dials, n.misrouted);
	}
}
}  // namespace

int main(int argc, char** argv) {
	const std::string_view only = argc > 1 ? argv[1] : "all";
	if (only == "mod") {
		use_mod = true;
		share(true);
		return 0;
	}
	if (only == "all" || only == "map") { map(); }
	if (only == "all" || only == "share") { share(true); }
	if (only == "remap-forget") {
		forget_lost = true;
		return remap() ? 0 : 1;
	}
	if (only == "all" || only == "remap") { CHECK(remap()); }
	if (only == "all" || only == "hello") { hello_checks(); }
	if (only == "all") {  // the controls: the F15 rule fails the share bound once L is not a power of two
		use_mod = true;
		CHECK(!share(false));
		use_mod = false;
		std::printf("control: slot mod L fails the share bound (expected) ok\n");
		forget_lost = true;  // D10 as at Detent:1ece2ff: a lost group appends again after the commit
		CHECK(!remap());
		forget_lost = false;
		std::printf("control: a re-partition that copies live records only forgets lost groups (expected) ok\n");
	}
	std::printf("placement_test: ok\n");
	return 0;
}
