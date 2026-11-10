// Review probe R2 (async-drivers-design, design lens), with D10's fix: loop j listens on base
// port + j only for j < L, so after a restart that shrinks L a dial computed from a stale view
// may find no listener. D10: a dial that is refused or fails its handshake falls back to the
// peer's loop 0 (base port, present for every L >= 1), whose hello carries the current L; the
// dialer closes with l_mismatch and redials the owning loop. For each restart this counts the
// groups whose first dial finds no listener, follows the fallback, and checks that every group
// reaches the loop that owns its slot. Exits 1 if any group does not.
//   shrink_dial [stale]   `stale`: the control, no fallback (D10 at Detent:1ece2ff); exits 1
#include <cstdint>
#include <cstdio>
#include <random>
#include <string_view>
#include <utility>

namespace {
std::uint32_t loop_of(std::uint32_t slot, std::uint32_t L) { return static_cast<std::uint32_t>((std::uint64_t{slot} * L) >> 12); }
std::uint32_t slot_of(std::uint64_t id) { return static_cast<std::uint32_t>((id * 0x9E3779B97F4A7C15ull) >> 52); }
}  // namespace

int main(int argc, char** argv) {
	const bool fallback = !(argc > 1 && std::string_view(argv[1]) == "stale");
	std::mt19937_64 random(5);
	std::uint64_t unreached_total = 0;
	for (auto [view, actual] : {std::pair{8u, 6u}, {16u, 3u}, {16u, 12u}, {3u, 1u}, {6u, 12u}}) {
		std::uint64_t unbound = 0, unreached = 0, groups = 100000;
		for (std::uint64_t n = 0; n < groups; ++n) {
			const std::uint32_t slot = slot_of(random());
			std::uint32_t target = loop_of(slot, view), seen_L = view;
			if (target >= actual) {  // base + target has no listener: no hello
				++unbound;
				if (!fallback) {
					++unreached;
					continue;
				}
				target = 0;  // loop 0 listens for every L
			}
			seen_L = actual;  // the hello of a listening loop carries the current L
			if (seen_L != view) { target = loop_of(slot, seen_L); }  // l_mismatch: redial the owning loop
			unreached += target != loop_of(slot, actual);
		}
		unreached_total += unreached;
		std::printf("shrink_dial%s view_L=%u actual_L=%u groups=%llu dialed_to_a_port_with_no_listener=%llu (%.1f%%) not_reaching_owning_loop=%llu\n",
			fallback ? "" : " (no fallback)", view, actual, static_cast<unsigned long long>(groups), static_cast<unsigned long long>(unbound),
			100.0 * double(unbound) / double(groups), static_cast<unsigned long long>(unreached));
	}
	const bool ok = unreached_total == 0;
	std::printf("shrink_dial: %s\n", ok ? "every stale dial reaches its owning loop through loop 0's hello ok"
	                                    : "FAILS: after a shrink, stale-view dials reach no listener, so no hello carries the new L");
	return ok ? 0 : 1;
}
