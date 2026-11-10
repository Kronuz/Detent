// Review X4: D10's placement with a loop count L that may change at restart, any L >= 1.
//   map     every L in 1..4,096: loop(slot, L) = (slot * L) >> 12 puts each of the 4,096 slots on
//           a loop below L, in contiguous ranges of floor or ceil(4,096 / L) slots
//   share   every pair L_a, L_b in 1..64: both ends compute the same loops for every slot from
//           (L_a, L_b) alone, and a loop of A shares slots with at most ceil(L_b / L_a) + 1 loops
//           of B, the Control connections it holds to that host
//   remap   restarts that change L (8->6, 6->12, 16->3, 3->1, 12->12) over a model of the turn
//           logs: open recovers every log of the stored generation, writes each group's live
//           records into its new loop's log of the next generation, syncs each, then commits by
//           replacing the layout record; a crash at every step recovers every group's records
//           exactly once, from the old generation before the commit and the new one after
//   hello   a dialer whose view of the peer's L is stale is closed with l_mismatch; a group-open
//           sent to a loop that does not own its slot is refused with misrouted
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
// records each group has there (synced logs only), and the layout record naming (L, generation).
struct record {
	std::uint64_t sequence;
	std::uint32_t bytes;
	bool operator==(const record&) const = default;
};
using group_records = std::map<std::uint64_t, std::vector<record>>;  // group id -> live records
struct disk {
	std::uint32_t L = 0, generation = 0;                                 // the layout record
	std::map<std::pair<std::uint32_t, std::uint32_t>, group_records> logs;  // (generation, loop) -> records
};

// Opens a host configured with L over `d`, running the re-partition if L changed. `crash_after`
// counts durable steps (a new log synced, the layout replaced, an old log deleted); the open stops
// there, as a crash would. Returns false if it crashed.
bool open(disk& d, std::uint32_t L, std::int64_t crash_after, std::uint64_t& copied) {
	std::int64_t steps = 0;
	auto step = [&] { return crash_after >= 0 && ++steps > crash_after; };
	// Recovery: the layout's generation is authoritative; a log of any other generation is a
	// re-partition that did not commit, or one whose old logs were not all deleted.
	for (auto it = d.logs.begin(); it != d.logs.end();) {
		it = it->first.first != d.generation ? d.logs.erase(it) : std::next(it);
	}
	if (d.L == L) { return true; }
	group_records live;
	for (const auto& [key, groups] : d.logs) {
		for (const auto& [g, records] : groups) {
			CHECK(!live.count(g));  // a group's records are in exactly one log
			live[g] = records;
		}
	}
	const std::uint32_t next = d.generation + 1;
	std::vector<group_records> fresh(L);
	for (const auto& [g, records] : live) {
		fresh[loop_of(slot_of(g), L)][g] = records;
		for (const auto& r : records) { copied += r.bytes; }
	}
	for (std::uint32_t i = 0; i < L; ++i) {  // write, then one barrier per new log
		if (step()) { return false; }
		d.logs[{next, i}] = fresh[i];
	}
	if (step()) { return false; }
	d.L = L;  // the commit point: the layout record names the new generation
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

// Every group's records, once each, on the loop its slot names under the layout's L.
void verify(const disk& d, const group_records& expected) {
	group_records seen;
	for (const auto& [key, groups] : d.logs) {
		CHECK(key.first == d.generation && key.second < d.L);
		for (const auto& [g, records] : groups) {
			CHECK(loop_of(slot_of(g), d.L) == key.second);
			CHECK(seen.emplace(g, records).second);
		}
	}
	CHECK(seen == expected);
}

std::pair<disk, group_records> host(std::uint32_t L, std::size_t groups, std::uint64_t seed) {
	std::mt19937_64 random(seed);
	disk d;
	d.L = L;
	d.generation = 1;
	group_records all;
	for (std::size_t n = 0; n < groups; ++n) {
		const std::uint64_t id = random();
		std::vector<record> records;
		for (std::uint64_t k = 1 + random() % 8, q = 1 + random() % 1000; k; --k) {
			records.push_back({q++, static_cast<std::uint32_t>(64 + random() % 4033)});
		}
		all[id] = records;
		d.logs[{1, loop_of(slot_of(id), L)}][id] = records;
	}
	return {d, all};
}

void remap() {
	for (auto [from, to] : {std::pair{8u, 6u}, {6u, 12u}, {16u, 3u}, {3u, 1u}, {12u, 12u}}) {
		auto [base, expected] = host(from, 2000, from * 131 + to);
		std::uint64_t live = 0;
		for (const auto& [g, records] : expected) {
			for (const auto& r : records) { live += r.bytes; }
		}
		int crashes = 0;
		for (std::int64_t crash = 0;; ++crash) {  // crash after each durable step, then reopen
			disk d = base;
			std::uint64_t copied = 0;
			if (open(d, to, crash, copied)) { break; }
			++crashes;
			if (d.generation == base.generation) {
				group_records old;  // before the commit point: the old generation is intact
				for (const auto& [key, groups] : d.logs) {
					if (key.first == base.generation) {
						for (const auto& [g, records] : groups) { old[g] = records; }
					}
				}
				CHECK(old == expected);
			}
			copied = 0;
			CHECK(open(d, to, -1, copied));
			verify(d, expected);
		}
		disk d = base;
		std::uint64_t copied = 0;
		CHECK(open(d, to, -1, copied));
		verify(d, expected);
		CHECK(copied == (from == to ? 0 : live));
		std::printf("remap L=%u->%u groups=2000 crash_points=%d live_bytes=%llu copied_bytes=%llu new_barriers=%u ok\n", from, to, crashes,
			static_cast<unsigned long long>(live), static_cast<unsigned long long>(copied), from == to ? 0 : to);
	}
}

// The connection hello carries (base port, L, loop index); a group-open carries the group id.
enum class verdict { served, l_mismatch, misrouted };
verdict hello(std::uint32_t view_L, std::uint32_t actual_L) { return view_L == actual_L ? verdict::served : verdict::l_mismatch; }
verdict group_open(std::uint64_t id, std::uint32_t receiving_loop, std::uint32_t actual_L) {
	return loop_of(slot_of(id), actual_L) == receiving_loop ? verdict::served : verdict::misrouted;
}

void hello_checks() {
	std::mt19937_64 random(5);
	std::size_t refused = 0, misrouted = 0, served = 0;
	for (auto [view, actual] : {std::pair{8u, 6u}, {6u, 12u}, {16u, 3u}, {12u, 12u}}) {
		for (std::uint32_t j = 0; j < std::max(view, actual); ++j) {
			if (hello(view, actual) == verdict::l_mismatch) { ++refused; }
		}
		for (int n = 0; n < 10000; ++n) {  // a group-open computed from the dialer's view
			const std::uint64_t id = random();
			const std::uint32_t target = loop_of(slot_of(id), view);
			if (target >= actual) {
				++misrouted;  // no such loop: its port refuses the connection
				continue;
			}
			const verdict v = group_open(id, target, actual);
			CHECK(v == (loop_of(slot_of(id), actual) == target ? verdict::served : verdict::misrouted));
			(v == verdict::served ? served : misrouted) += 1;
			if (view == actual) { CHECK(v == verdict::served); }
		}
	}
	CHECK(refused > 0 && misrouted > 0);
	std::printf("hello stale views refused with l_mismatch=%zu; group-opens from a stale view misrouted=%zu, served=%zu (only where the loop owns the slot) ok\n",
		refused, misrouted, served);
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
	if (only == "all" || only == "remap") { remap(); }
	if (only == "all" || only == "hello") { hello_checks(); }
	if (only == "all") {  // the control: the F15 rule fails the share bound once L is not a power of two
		use_mod = true;
		CHECK(!share(false));
		use_mod = false;
		std::printf("control: slot mod L fails the share bound (expected) ok\n");
	}
	std::printf("placement_test: ok\n");
	return 0;
}
