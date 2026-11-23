#include "managed_host_internal.hh"
#include <stdexcept>

namespace t = detent::delivery;
using detent_agent::detail::may_announce_ready;
void check(bool value) {
	if (!value) throw std::runtime_error("managed readiness invariant");
}
// Ready once initialized and every followed namespace is ready (C4), never while closing or fenced.
int main() {
	t::CoordinatorStatus state;
	state.phase = t::CoordinatorPhase::Recovering;
	check(!may_announce_ready(state));
	state.initialized = true;
	check(!may_announce_ready(state));
	state.ready = true;
	check(may_announce_ready(state));
	state.initialized = false;
	check(!may_announce_ready(state));
	state.initialized = true;
	for (const auto phase : {t::CoordinatorPhase::Closing, t::CoordinatorPhase::Closed, t::CoordinatorPhase::Fenced}) {
		state.phase = phase;
		check(!may_announce_ready(state));
	}
}
