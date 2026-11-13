// F1 (namespace-s6-cutover correctness review): a voter that has not yet applied a drop and
// re-creation answers a store already at the new incarnation. Positions continue across
// incarnations, so the store is above that voter's head: Ahead, no frame, never a full copy of the
// dropped incarnation (D1).
#include "change_history.hh"
#include "cluster_harness.hh"
#include "detent_distribution/session_frames.hh"
#include "history.hh"
#include "namespace_history.hh"
#include <iostream>

using namespace detent::authority;
using namespace kronuz::consensus;
using detent::authority::testing::Cluster;
namespace d = detent::distribution;
namespace {
std::uint64_t sequence = 0, request = 100;
std::array<std::shared_ptr<ChangeHistory>, 3> rings;
void commit(Cluster &group, const std::string &name, const std::string &key, int value) {
	Command c;
	c.context = group.context;
	c.producer = "writer";
	c.generation = 1;
	c.sequence = ++sequence;
	c.name_space = name;
	c.mutations.push_back({name + ":" + key, detent::KnobDef{value}, {}, {}});
	auto result = group.request(0, ++request, c).results.front();
	if (result.status != Status::Applied)
		std::abort();
}
void settle(Cluster &group, std::initializer_list<unsigned> voters) {
	group.until([&] {
		for (auto i : voters) {
			auto &n = *group.nodes[i];
			if (n.state().applied_index() != group.nodes[0]->state().applied_index() || n.worker().busy() ||
				n.state().applied_index() != n.worker().committed())
				return false;
		}
		return true;
	});
	group.step();
}
std::unique_ptr<AuthorityHistory> freeze(Cluster &group, unsigned i, const std::string &name) {
	auto &node = *group.nodes[i];
	return AuthorityHistory::freeze(name, group.context.cluster, node.state(), *node.sealed(),
									node.history()->layout().table(), *node.history_files(), *rings[i]);
}
} // namespace

int main() {
	LayoutPolicy policy;
	policy.seal_transactions = 6;
	policy.seal_bytes = 64 * 1024;
	policy.period = 4;
	policy.carry_bytes = 64;
	policy.carry_count = 2;
	policy.alpha_num = 1;
	policy.alpha_den = 2;
	testing::history_policy = policy;
	testing::memory_storage = true;
	testing::ring = [](unsigned i) {
		return rings[i] = std::make_shared<ChangeHistory>(std::make_shared<d::StorageLedger>(64 * 1024 * 1024),
														  HistoryOptions{256, 4 * 1024 * 1024, 65536});
	};
	Cluster group;
	group.until([&] { return group.nodes[0]->worker().role() == Role::Leader; });
	Command open;
	open.context = group.context;
	open.kind = Kind::Open;
	open.producer = "writer";
	open.nonce[0] = 'n';
	group.request(0, 1, open);
	group.link(0, 1, true);
	group.link(1, 2, true);
	AdministrativeCommand enable{group.context, AdministrativeKind::EnablePositions, {}, {}, {}, {}};
	enable.nonce[0] = 's';
	if (group.administer(0, 2, enable).results[0].status != Status::Applied)
		std::abort();
	for (int i = 0; i < 10; ++i)
		commit(group, "c", "k" + std::to_string(i % 3), i);
	settle(group, {0, 1, 2});

	// Voter 2 stops hearing the group; the drop and the re-creation commit on voters 0 and 1.
	group.link(0, 2, false);
	group.link(1, 2, false);
	const auto *c = group.nodes[0]->state().namespace_entry("c");
	AdministrativeCommand drop{group.context, AdministrativeKind::DropNamespace, {}, "c", c->incarnation, {}};
	if (group.administer(0, ++request, drop).results[0].status != Status::Applied)
		std::abort();
	commit(group, "c", "fresh", 42);
	settle(group, {0, 1});

	const auto renewed = freeze(group, 0, "c")->head();
	auto lagging = freeze(group, 2, "c");
	const auto old = lagging->head();
	std::cout << "leader head: incarnation " << int(renewed.incarnation[0]) << "... position " << renewed.position
			  << "; lagging voter head: incarnation " << int(old.incarnation[0]) << "... position " << old.position
			  << " (same incarnation: " << (renewed.incarnation == old.incarnation) << ")\n";
	if (renewed.incarnation == old.incarnation || renewed.position <= old.position) {
		std::cout << "probe setup failed: the lagging voter is not behind the re-creation\n";
		return 2;
	}
	const auto answer = d::select_answer(*lagging, renewed);
	d::NamespaceRequest one{"c", lagging.get(), renewed};
	const auto bytes = d::encode_session_frames(std::nullopt, {&one, 1});
	std::cout << "lagging voter's answer to the store at the new incarnation: kind " << int(answer.kind) << " ("
			  << (answer.kind == d::AnswerKind::FullCopy ? "FullCopy" : answer.kind == d::AnswerKind::Ahead ? "Ahead"
																												: "other")
			  << "), " << bytes.size() << " bytes\n";
	if (answer.kind != d::AnswerKind::Ahead || bytes != std::string("\x93\x01\xc0\x00", 4)) {
		std::cout << "DEFECT: the store at position " << renewed.position << " of the new incarnation is not Ahead"
				  << " of the lagging voter at position " << old.position << "\n";
		return 1;
	}
	return 0;
}
