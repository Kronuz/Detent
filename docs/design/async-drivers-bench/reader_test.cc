// Review F12 (2): head-of-line blocking on a connection a loop's groups share (D10). Group 1's
// admission is full for 3 s (its Worker answers Busy); group 2 is a follower whose leader sends
// a heartbeat every 100 ms on the same connection, with group 1's appends between them.
//   today     the reader holds one decoded input and waits for that group's admission before it
//             reads again (transport/peers.h:368-390): group 2's heartbeats wait behind group 1
//   (default) D10's reader: a frame for a group whose input slot is full is dropped and counted
//             (Raft resends), and the reader goes on: group 2 sees every heartbeat on time
// Fails (exit 1) when group 2 goes longer than its 1,000 ms minimum election timeout without
// a heartbeat, which would start an election.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string_view>
#include <vector>

namespace {
struct frame {
	int group;
	std::uint64_t sent_ms;
};

struct result {
	std::uint64_t longest_gap_ms = 0;  // group 2's longest time without a delivered heartbeat
	int delivered_1 = 0, dropped_1 = 0, delivered_2 = 0;
};

result run(bool today) {
	constexpr std::uint64_t busy_until = 3'000, end = 5'000;
	std::deque<frame> wire;  // in send order: group 1's appends every 10 ms, group 2's heartbeat every 100 ms
	for (std::uint64_t t = 0; t < end; t += 10) {
		wire.push_back({1, t});
		if (t % 100 == 0) { wire.push_back({2, t}); }
	}
	result r;
	std::uint64_t now = 0, last_heartbeat = 0;
	bool slot_1_full = true;  // group 1's slot holds one input its Worker has not admitted
	while (!wire.empty()) {
		frame f = wire.front();
		now = std::max(now, f.sent_ms);
		if (now >= busy_until) { slot_1_full = false; }  // group 1 admits again
		if (f.group == 1 && slot_1_full) {
			if (today) {  // the reader waits for this group's admission before reading the next frame
				now = busy_until;
				continue;
			}
			++r.dropped_1;  // D10: dropped and counted; group 1's leader resends after rpc_timeout
			wire.pop_front();
			continue;
		}
		wire.pop_front();
		if (f.group == 1) {
			++r.delivered_1;
		} else {
			r.longest_gap_ms = std::max(r.longest_gap_ms, now - last_heartbeat);
			last_heartbeat = now;
			++r.delivered_2;
		}
	}
	return r;
}
}  // namespace

int main(int argc, char** argv) {
	const bool today = argc > 1 && std::string_view(argv[1]) == "today";
	const result r = run(today);
	const bool ok = r.longest_gap_ms < 1'000;
	std::printf("reader=%s group2_heartbeats=%d group2_longest_gap_ms=%llu group1_delivered=%d group1_dropped=%d %s\n",
		today ? "today" : "d10", r.delivered_2, static_cast<unsigned long long>(r.longest_gap_ms), r.delivered_1, r.dropped_1,
		ok ? "ok" : "FAILS: group 2 would start an election");
	if (!ok) { return 1; }
	std::printf("reader_test: ok\n");
	return 0;
}
