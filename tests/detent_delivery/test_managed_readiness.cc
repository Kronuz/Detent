#include "managed_host_internal.hh"
#include <stdexcept>

namespace t = detent::delivery;
namespace l = detent::local;
using detent_agent::detail::may_announce_ready;
void check(bool value) {
	if (!value) throw std::runtime_error("managed readiness invariant");
}
int main() {
	t::CoordinatorStatus state;
	state.phase = t::CoordinatorPhase::Recovering;
	check(!may_announce_ready(state, true));
	state.initialized = true;
	state.selected.knowledge = l::SelectionKnowledge::Absent;
	check(may_announce_ready(state, true) && may_announce_ready(state, false));
	state.selected.knowledge = l::SelectionKnowledge::Uncertain;
	check(!may_announce_ready(state, true) && !may_announce_ready(state, false));
	state.prepared_ready = true;
	check(!may_announce_ready(state, true));
	state.selected.knowledge = l::SelectionKnowledge::Known;
	check(!may_announce_ready(state, true) && !may_announce_ready(state, false));
	state.selected.descriptor.emplace();
	check(may_announce_ready(state, true) && may_announce_ready(state, false));
	state.prepared_ready = false;
	check(!may_announce_ready(state, true) && may_announce_ready(state, false));
	state.prepared_ready = true;
	for (const auto phase : {t::CoordinatorPhase::Closing, t::CoordinatorPhase::Closed, t::CoordinatorPhase::Fenced}) {
		state.phase = phase;
		check(!may_announce_ready(state, true) && !may_announce_ready(state, false));
	}
}
