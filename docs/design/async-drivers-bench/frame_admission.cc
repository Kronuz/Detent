// Xapiand review 2, X4: which frames a shared node connection admits (D10, Q7). A cluster of
// consensus Cores, as pinned, exchanges messages through one FIFO (the harness of consensus's
// test/membership_test.cc); each delivery first passes the receiving node's admission rule:
//   stored   D10's rule: the group is stored here and holds the destination incarnation; where
//            the receiver's membership names the source incarnation, its node is the
//            connection's authenticated node; Core decides the rest
//   voters   the rule at Detent:73e96d6: only a sender that votes in the receiver's membership
// Scenarios, each run to a liveness check:
//   joiner   a joiner (empty membership) is added as a learner, catches up and is promoted
//   lagging  a follower cut off while 4 joins and takes over by transfer catches up from 4
//            (the rule under test admits node 3's frames; the others use `stored`)
//   removal  a voter is removed and the group commits without it
//   spoof    a frame on node 4's connection naming incarnation 2 is refused (stored only)
// Prints one line per scenario; `run.sh frame_admission` exits 0 when every scenario holds,
// `frame_admission voters` exits 1 when one fails (the control).
#include "consensus/core.h"
#include "consensus/membership.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace kronuz::consensus;

namespace {
enum class rule { stored, voters };
rule admission = rule::stored;

constexpr Timing compact{20, 40, 100, 200, 60000};

FixedConfiguration founding_config(NodeId local, std::size_t voters = 3) {
	FixedConfiguration c;
	c.cluster[0] = 'P';
	c.configuration[0] = 'R';
	c.local = local;
	for (NodeId v = 1; v <= voters; ++v) { c.voters.push_back(v); }
	return c;
}
FixedConfiguration joiner_config(NodeId local) {
	auto c = founding_config(local);
	c.voters.clear();
	return c;
}

struct Node {
	std::unique_ptr<Core> core;
	bool down = false;
};
struct Envelope {
	NodeId connection;  // the node the connection authenticated
	NodeId from, to;    // source and destination incarnations (incarnation k lives on node k here)
	Message message;
};

class Cluster {
public:
	std::map<NodeId, Node> nodes;
	std::deque<Envelope> wire;
	std::set<std::pair<NodeId, NodeId>> cut;
	std::uint64_t now = 0, refused = 0;
	NodeId probed = 0;  // when set, the rule under test admits only this node's frames; the others use `stored`
	RequestId request = 1000;

	void add(NodeId id, FixedConfiguration config, RecoveredState state) {
		auto& n = nodes[id];
		n.core = std::make_unique<Core>(config, std::move(state), Limits{}, compact);
		consume(id, n.core->step(Tick{now, 100 + 20 * id}));
		consume(id, n.core->step(Start{}));
	}
	void add_founding(NodeId id, std::size_t voters) {
		RecoveredState s;
		s.configuration = founding_config(id, voters);
		add(id, founding_config(id, voters), std::move(s));
	}
	void add_joiner(NodeId id) {
		RecoveredState s;
		s.configuration = joiner_config(id);
		s.membership = Membership{};
		add(id, joiner_config(id), std::move(s));
	}
	Core& core(NodeId id) { return *nodes.at(id).core; }
	void consume(NodeId id, Actions actions) {
		for (auto& a : actions) {
			if (auto p = std::get_if<Persist>(&a)) {
				consume(id, core(id).step(Persisted{p->token}));
			} else if (auto s = std::get_if<Send>(&a)) {
				wire.push_back({id, id, s->peer, s->message});
			} else if (auto c = std::get_if<Committed>(&a)) {
				consume(id, core(id).step(Applied{c->entries.back().index}));
			} else if (auto pc = std::get_if<PersistCheckpoint>(&a)) {
				consume(id, core(id).step(Persisted{pc->token}));
			}
		}
	}
	bool admit(const Envelope& e) {
		const auto& membership = core(e.to).membership();
		if (admission == rule::voters && (!probed || e.to == probed)) { return votes_in(membership, e.from); }
		const Member* source = find_member(membership, e.from);
		return !source || source->node == e.connection;
	}
	void pump(std::size_t bound = 100000) {
		while (!wire.empty() && bound--) {
			auto e = std::move(wire.front());
			wire.pop_front();
			if (!nodes.contains(e.to)) { continue; }  // not stored here: dropped
			auto& n = nodes.at(e.to);
			if (n.down || nodes.at(e.from).down || cut.contains({e.from, e.to})) { continue; }
			if (!admit(e)) {
				++refused;
				continue;
			}
			consume(e.to, n.core->step(Tick{now, 100 + 20 * e.to}));
			consume(e.to, n.core->step(Receive{e.from, std::move(e.message)}));
		}
	}
	void tick(std::uint64_t dt = 10) {
		now += dt;
		for (auto& [id, n] : nodes) {
			if (!n.down) { consume(id, n.core->step(Tick{now, 100 + 20 * id})); }
		}
		pump();
	}
	void run(std::uint64_t ms) {
		for (std::uint64_t t = 0; t < ms; t += 10) { tick(10); }
	}
	std::optional<NodeId> leader() {
		std::optional<NodeId> best;
		for (auto& [id, n] : nodes) {
			if (!n.down && n.core->role() == Role::Leader && (!best || n.core->term() > core(*best).term())) { best = id; }
		}
		return best;
	}
	bool commit_one(std::string command, std::uint64_t ms = 5000) {
		std::optional<Index> placed;
		NodeId at = 0;
		for (std::uint64_t t = 0; t < ms; t += 10) {
			if (auto l = leader(); l && !placed && !core(*l).busy()) {
				at = *l;
				auto actions = core(at).step(Propose{++request, command});
				for (const auto& a : actions) {
					if (auto p = std::get_if<Persist>(&a); p && p->batch.log) { placed = p->batch.log->replace_from; }
				}
				consume(at, std::move(actions));
			}
			tick(10);
			if (placed && core(at).committed() >= *placed) { return true; }
		}
		return false;
	}
	bool change(NodeId leader_id, Membership next) {
		auto actions = core(leader_id).step(ChangeMembership{++request, std::move(next)});
		const bool rejected = std::any_of(actions.begin(), actions.end(), [](const Action& a) { return std::holds_alternative<Reject>(a); });
		consume(leader_id, std::move(actions));
		pump();
		return !rejected;
	}
	Membership with_learner(NodeId leader_id, NodeId id) {
		Membership m = core(leader_id).membership();
		m.members.push_back({id, id, MemberRole::Learner});
		m.next_incarnation = id + 1;
		return m;
	}
	Membership promoted(NodeId leader_id, NodeId id) {
		Membership m = core(leader_id).membership();
		for (auto& x : m.members) {
			if (x.incarnation == id) { x.role = MemberRole::Voter; }
		}
		return m;
	}
	bool caught_up(NodeId follower, NodeId leader_id) { return core(follower).committed() == core(leader_id).committed(); }
};

Cluster founded(std::size_t voters = 3) {
	Cluster c;
	for (NodeId id = 1; id <= voters; ++id) { c.add_founding(id, voters); }
	c.now = 100;
	c.consume(1, c.core(1).step(Tick{c.now, 100}));  // node 1's timer fires first
	c.run(400);
	return c;
}

bool report(const char* name, bool ok, const Cluster& c, const char* detail) {
	std::printf("%s rule=%s refused=%llu %s: %s\n", name, admission == rule::stored ? "stored" : "voters",
		static_cast<unsigned long long>(c.refused), detail, ok ? "ok" : "FAILS");
	return ok;
}

// A joiner's membership is empty until the entry naming it arrives; as a learner it never votes.
bool joiner() {
	auto c = founded();
	c.add_joiner(4);
	bool ok = c.leader() == 1u && c.commit_one("a");
	ok = ok && c.change(1, c.with_learner(1, 4));
	c.run(500);
	const bool learned = c.caught_up(4, 1) && votes_in(c.core(4).membership(), 1);
	ok = ok && learned && c.change(1, c.promoted(1, 4));
	c.run(500);
	const bool promoted = ok && votes_in(c.core(1).committed_membership(), 4);
	return report("joiner", promoted, c, learned ? "learner caught up, promotion committed" : "learner 4 never caught up");
}

// 3 misses 4's addition and promotion; 4 then leads by transfer. 3's membership never names 4.
bool lagging() {
	auto c = founded();
	c.probed = 3;
	bool ok = c.leader() == 1u && c.commit_one("a");
	c.add_joiner(4);
	for (NodeId other : {1, 2, 4}) {
		c.cut.insert({other, 3});
		c.cut.insert({3, other});
	}
	ok = ok && c.change(1, c.with_learner(1, 4));
	c.run(500);
	ok = ok && c.change(1, c.promoted(1, 4));
	c.run(500);
	c.consume(1, c.core(1).step(TransferLeadership{++c.request, 4}));
	c.run(500);
	ok = ok && c.leader() == 4u && !find_member(c.core(3).membership(), 4);
	c.cut.clear();
	ok = ok && c.commit_one("b");
	c.run(1000);
	const bool caught = ok && c.caught_up(3, 4) && votes_in(c.core(3).membership(), 4);
	return report("lagging", caught, c, caught ? "3 caught up from leader 4" : "3 never caught up from leader 4");
}

bool removal() {
	auto c = founded(4);
	bool ok = c.leader() == 1u && c.commit_one("a");
	Membership without4 = c.core(1).membership();
	std::erase_if(without4.members, [](const Member& x) { return x.incarnation == 4; });
	ok = ok && c.change(1, without4);
	c.run(500);
	ok = ok && !find_member(c.core(1).committed_membership(), 4) && c.commit_one("b");
	return report("removal", ok, c, ok ? "removal committed, group commits without 4" : "removal did not commit");
}

// Node 4's connection carries a frame naming incarnation 2, which the receiver's membership puts on node 2.
bool spoof() {
	auto c = founded();
	bool ok = c.leader() == 1u && c.commit_one("a");
	const auto before = c.refused;
	const Term term = c.core(3).term();
	c.wire.push_back({4, 2, 3, VoteRequest{term + 5, 1000, term, false}});
	c.pump();
	const bool refused = c.refused == before + 1;
	ok = ok && refused && c.core(3).term() == term;
	return report("spoof", ok, c, refused ? "refused, 3's term unchanged" : "admitted");
}
}  // namespace

int main(int argc, char** argv) {
	const std::string_view mode = argc > 1 ? argv[1] : "stored";
	admission = mode == "voters" ? rule::voters : rule::stored;
	bool ok = joiner();
	ok = lagging() && ok;
	ok = removal() && ok;
	if (admission == rule::stored) { ok = spoof() && ok; }
	std::printf("frame_admission rule=%.*s: %s\n", int(mode.size()), mode.data(), ok ? "ok" : "FAILS");
	return ok ? 0 : 1;
}
