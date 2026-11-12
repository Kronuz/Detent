// S5.1 A4: three voters apply the enabling entry, position updates and a drop
// identically, with one voter installed from a snapshot and one restarted mid-run.
#include "cluster_harness.hh"
#include "namespace_oracle.hh"
#include <iostream>

using namespace detent::authority;
using namespace kronuz::consensus;
using detent::authority::testing::Cluster;
using detent::authority::testing::completion_storage;
using detent::authority::testing::same_namespaces;
namespace {
std::uint64_t sequence = 0, request = 100;
Command write(Cluster &group, std::string name, std::string key, int value) {
	Command c;
	c.context = group.context;
	c.producer = "writer";
	c.generation = 1;
	c.sequence = ++sequence;
	c.name_space = name;
	c.mutations.push_back({name + ":" + key, detent::KnobDef{value}, {}, {}});
	return c;
}
Result commit(Cluster &group, unsigned leader, Command command) {
	auto result = group.request(leader, ++request, command).results.front();
	assert(result.status == Status::Applied || result.status == Status::NoChange);
	return result;
}
void settle(Cluster &group) {
	group.until([&] {
		return std::all_of(group.nodes.begin(), group.nodes.end(), [&](const auto &node) {
			return node->state().applied_index() == group.nodes[0]->state().applied_index() &&
				   !node->worker().busy() && node->state().applied_index() == node->worker().committed();
		});
	});
}
} // namespace
int main(int argc, char **argv) {
	completion_storage = argc == 2 && std::string_view(argv[1]) == "--completion-storage";
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
	// A store written before the enabling entry.
	for (int i = 0; i < 6; ++i)
		commit(group, 0, write(group, i % 2 ? "b" : "a", "k" + std::to_string(i % 3), i));
	assert(group.nodes[0]->state().namespace_revision("a") == 5 &&
		   group.nodes[0]->state().namespace_revision("b") == 6);

	// Voter 1 misses the enabling entry, the drop and the re-creation; it installs a snapshot.
	group.link(0, 1, false);
	group.link(1, 2, false);
	const auto missed = group.nodes[1]->state().applied_index();
	AdministrativeCommand enable{group.context, AdministrativeKind::EnablePositions, {}, {}};
	enable.nonce[0] = 's';
	assert(group.administer(0, 2, enable).results[0].status == Status::Applied);
	for (int i = 0; i < 40; ++i)
		commit(group, 0, write(group, i % 3 == 0 ? "a" : i % 3 == 1 ? "b" : "c", "k" + std::to_string(i % 5), 100 + i));
	AdministrativeCommand drop{group.context, AdministrativeKind::DropNamespace, {}, "b",
							   group.nodes[0]->state().namespace_entry("b")->incarnation};
	assert(group.administer(0, 3, drop).results[0].status == Status::Applied);
	commit(group, 0, write(group, "b", "again", 1));
	group.until(
		[&] { return group.nodes[0]->worker().applied_index() == group.nodes[0]->state().applied_index(); });
	assert(group.nodes[0]->checkpoint());
	group.until([&] { return !group.nodes[0]->checkpoint_busy(); });
	for (int i = 0; i < 3; ++i) // The leader compacted through its checkpoint: voter 1 installs it.
		commit(group, 0, write(group, "a", "hot", i));
	// The leader's log starts past what voter 1 applied, so only an install catches it up.
	assert(group.nodes[0]->worker().base_index() > missed);
	group.link(0, 1, true);
	group.link(1, 2, true);
	settle(group);
	assert(group.nodes[1]->worker().base_index() > missed);
	assert(group.nodes[1]->state().positions_enabled());

	// Voter 2 restarts mid-run and replays the enabling entry and the drop from its journal.
	for (int i = 0; i < 10; ++i)
		commit(group, 0, write(group, "c", "k" + std::to_string(i), 200 + i));
	group.link(0, 2, false);
	group.link(1, 2, false);
	group.nodes[2].reset();
	group.messages[2].clear();
	for (int i = 0; i < 10; ++i)
		commit(group, 0, write(group, i % 2 ? "c" : "d", "k" + std::to_string(i), 300 + i));
	group.launch(2, true);
	group.link(0, 2, true);
	group.link(1, 2, true);
	for (int i = 0; i < 10; ++i)
		commit(group, 0, write(group, "a", "k" + std::to_string(i), 400 + i));
	settle(group);

	// Identical positions, incarnations, sizes, directory, records and images on every voter.
	const auto &leader = group.nodes[0]->state();
	for (unsigned i = 1; i < 3; ++i) {
		same_namespaces(leader, group.nodes[i]->state());
		assert(leader.checkpoint() == group.nodes[i]->state().checkpoint());
	}
	// Positions advanced only with each namespace's own changes, from its stored revision.
	assert(leader.namespace_entry("a")->position == 5 + 14 + 3 + 10);
	assert(leader.namespace_entry("c")->position == 13 + 10 + 5 && leader.namespace_entry("d")->position == 5);
	auto b = *leader.namespace_entry("b");
	assert(b.live && b.position == 6 + 13 + 1 + 1 && b.directory == 5 && leader.directory_position() == 6);
	assert(leader.positions_enabled() && leader.find("b:again") && !leader.find("b:k0"));
	std::cout << "three voters agree on namespace positions across an install and a restart\n";
}
