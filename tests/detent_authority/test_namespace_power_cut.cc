// S5.1 A5, S5.2 A5: a power cut at every storage primitive of a run that writes
// before the enabling entry, applies it, updates positions, checkpoints, drops a
// namespace and changes the roll-up period, with history on the same disk.
// Recovery, with no peer to fetch from, then the same producer retries, must
// reach the state and the layout of the uninterrupted run, and nothing
// acknowledged before the cut may be lost: no checkpoint outran its history.
#include "namespace_oracle.hh"
#include "node.hh"
#include <cassert>
#include <iostream>
#include <journal/memory_io.h>
#include <variant>

using namespace detent::authority;
using namespace kronuz::consensus;
namespace mem = kronuz::journal::testing;
namespace {
Context context() {
	Context c;
	c.cluster[0] = 'c';
	c.group[0] = 'g';
	c.epoch[0] = 'e';
	return c;
}
NodeOptions options() {
	NodeOptions o;
	o.context = context();
	o.configuration.cluster = o.context.cluster;
	o.configuration.configuration[0] = 'v';
	o.configuration.local = 1;
	o.configuration.voters = {1};
	o.consensus.log_bytes = 8 * 1024 * 1024;
	o.consensus.log_entries = 128;
	o.application.producers = 16;
	o.application.namespaces = 64;
	o.application.results = 64;
	o.application.state_bytes = 2 * 1024 * 1024;
	o.application.image_bytes = 4 * 1024 * 1024;
	o.application.layout.seal_transactions = 1; // Checkpoints are taken at seals: one per change.
	o.application.layout.period = 4;
	o.timing.heartbeat = 100;
	o.timing.rpc_timeout = 200;
	o.timing.election_min = 1000;
	o.timing.election_max = 2000;
	return o;
}
struct Checkpoint {};
LayoutPolicy period(std::uint64_t p) {
	auto policy = options().application.layout;
	policy.period = p;
	return policy;
}
using Step = std::variant<Command, AdministrativeCommand, Checkpoint>;
Command write(std::uint64_t sequence, std::string name, std::string key, std::optional<int> value) {
	Command c;
	c.context = context();
	c.producer = "writer";
	c.generation = 1;
	c.sequence = sequence;
	c.name_space = name;
	c.mutations.push_back({name + ":" + key,
						   value ? std::optional<detent::KnobDef>(detent::KnobDef{*value}) : std::nullopt,
						   {},
						   {}});
	return c;
}
std::vector<Step> workload() {
	Command open;
	open.context = context();
	open.kind = Kind::Open;
	open.producer = "writer";
	open.nonce[0] = 'n';
	AdministrativeCommand enable{context(), AdministrativeKind::EnablePositions, {}, {}};
	enable.nonce[0] = 's';
	std::uint64_t s = 0;
	return {open,
			write(++s, "a", "x", 1),
			write(++s, "b", "x", 2),
			write(++s, "a", "y", 3),
			Checkpoint{},
			enable, // The enabling entry over a store written before it.
			write(++s, "b", "y", 4),
			write(++s, "c", "x", 5),
			write(++s, "a", "x", std::nullopt),
			Checkpoint{},
			write(++s, "a", "z", 6),
			AdministrativeCommand{context(), AdministrativeKind::DropNamespace, {}, "b",
								  derive_incarnation(context(), enable.nonce, 2, "b")},
			Checkpoint{}, // An image holding a tombstone.
			AdministrativeCommand{context(), AdministrativeKind::SetLayout, {}, {}, {}, period(2)},
			write(++s, "b", "x", 7), // Re-created under a new incarnation.
			write(++s, "c", "x", 8),
			Checkpoint{},
			write(++s, "c", "y", 9)};
}
struct Run {
	mem::Disk &disk;
	std::unique_ptr<Node> node;
	std::uint64_t clock = 0, request = 1;
	std::vector<Completion> completed;
	void pump() {
		node->service({++clock, 1000});
		if (auto job = node->worker().take_io_operation())
			kronuz::journal::drive_synchronously(*job);
		if (auto job = node->take_history_operation()) {
			kronuz::journal::drive_synchronously(*job);
			if (disk.dead())
				throw mem::PowerLoss{};
		}
		node->take_network_actions();
		for (auto &c : node->take_completions())
			completed.push_back(std::move(c));
	}
	template <class F> void until(F &&f) {
		for (unsigned i = 0; i < 200000 && !f(); ++i)
			pump();
		assert(f());
	}
	// History writes queued behind apply land.
	void settle() {
		until([&] { return node->history()->durable_through(node->sealed()->applied_index()); });
	}
	bool created = false, strict = true;
	void boot(bool recover) {
		node = std::make_unique<Node>(std::make_unique<mem::MemoryIO>(disk), options());
		node->attach_history(std::make_shared<mem::MemoryIO>(disk, "history/"));
		if (recover)
			node->recover();
		else {
			Identity id{};
			id[0] = 'a';
			node->create(id);
		}
		created = true;
		until([&] { return node->worker().ready(); });
		node->start();
		until([&] { return node->worker().role() == Role::Leader; });
		until([&] { return node->state().applied_index() == node->worker().committed(); });
	}
	// Runs one step to its acknowledgement; a proposal the leader dropped is retried.
	void run(const Step &step) {
		if (std::holds_alternative<Checkpoint>(step)) {
			// A retried run may find nothing new to checkpoint; the reference run must.
			bool started = false;
			for (unsigned i = 0; i < 20000 && !started; ++i) {
				started = node->worker().applied_index() == node->state().applied_index() && node->checkpoint();
				if (!started)
					pump();
			}
			assert(started || !strict);
			until([&] { return !node->checkpoint_busy(); });
			return;
		}
		for (;;) {
			auto id = ++request;
			bool submitted = false;
			until([&] {
				if (!submitted) {
					auto result = std::holds_alternative<Command>(step)
									  ? node->propose(id, {std::get<Command>(step)})
									  : node->propose_administrative(id, std::get<AdministrativeCommand>(step));
					submitted = result == SubmitResult::Accepted;
				}
				return submitted && std::any_of(completed.begin(), completed.end(),
												[&](auto &c) { return c.request == id; });
			});
			auto found = std::find_if(completed.begin(), completed.end(), [&](auto &c) { return c.request == id; });
			auto rejected = found->rejection.has_value();
			completed.erase(found);
			if (!rejected)
				return;
		}
	}
};
// The layout reaches every namespace at its position, through a durable history.
void same_layout(const State &expected, const Node &node) {
	const auto *history = node.history();
	assert(history && history->ready() && node.sealed());
	assert(history->durable_through(node.sealed()->applied_index()));
	const auto &table = history->layout().table();
	const auto &sealed = *node.sealed();
	assert(table.policy == sealed.policy() &&
		   (sealed.policy().period == 2 || (sealed.pending_policy() && sealed.pending_policy()->period == 2)));
	for (const auto &n : table.namespaces) {
		const auto *entry = node.sealed()->namespace_entry(n.name);
		assert(entry && entry->live && entry->incarnation == n.incarnation && entry->position == n.position);
		assert(n.live == entry->bytes);
	}
	std::size_t live = 0;
	expected.for_each_directory_entry(0, [&](auto, auto &, auto &entry) { live += entry.live; });
	assert(table.namespaces.size() == live);
}
} // namespace
int main() {
	const auto steps = workload();
	mem::Disk reference;
	State expected(context());
	std::uint64_t operations = 0;
	{
		Run run{reference};
		run.boot(false);
		for (const auto &step : steps)
			run.run(step);
		expected = run.node->state();
		run.settle();
		same_layout(expected, *run.node);
		operations = reference.operations;
		run.node.reset();
	}
	assert(expected.positions_enabled() && expected.namespace_entry("a")->position == 5 &&
		   expected.namespace_entry("b")->position == 5 && expected.namespace_entry("b")->live &&
		   expected.namespace_entry("c")->position == 3 && expected.directory_position() == 5);
	unsigned cuts = 0, enabling_acknowledged = 0;
	for (std::uint64_t cut = 1; cut <= operations; ++cut) {
		mem::Disk disk;
		disk.cut = cut;
		disk.random.seed(cut);
		std::size_t acknowledged = 0;
		bool fresh = true;
		{
			Run run{disk};
			try {
				run.boot(false);
				for (const auto &step : steps) {
					run.run(step);
					++acknowledged;
				}
			} catch (const mem::PowerLoss &) {
			}
			fresh = !run.created;
			run.node.reset();
		}
		if (!disk.dead())
			continue; // The cut fell past this run's operations.
		++cuts;
		disk.power_loss();
		Run run{disk};
		run.strict = false;
		if (fresh) {
			// The cut fell inside creation: a store that never became ready starts over.
			disk = mem::Disk{};
			run.boot(false);
		} else
			run.boot(true);
		// Every acknowledged step survives: the producer's sequence and the enabling entry.
		std::uint64_t sequences = 0;
		bool enabled = false;
		for (std::size_t i = 0; i < acknowledged; ++i) {
			if (auto c = std::get_if<Command>(&steps[i]); c && c->kind == Kind::Transaction)
				sequences = c->sequence;
			if (auto a = std::get_if<AdministrativeCommand>(&steps[i]);
				a && a->kind == AdministrativeKind::EnablePositions)
				enabled = true;
		}
		const auto *producer = run.node->state().producer("writer");
		assert(!sequences || (producer && producer->high_water >= sequences));
		assert(!enabled || run.node->state().positions_enabled());
		enabling_acknowledged += enabled;
		for (const auto &step : steps)
			run.run(step);
		detent::authority::testing::same_namespaces(expected, run.node->state());
		run.settle();
		same_layout(expected, *run.node);
		run.node.reset();
	}
	assert(cuts > 50 && enabling_acknowledged > 0);
	std::cout << "namespace positions recover to the uninterrupted state after " << cuts << " power cuts over "
			  << operations << " storage operations\n";
}
