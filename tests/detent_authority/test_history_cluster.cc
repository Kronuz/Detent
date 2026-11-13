// S5.2 T4(1, 2, 4): three voters write byte-identical seal files, blocks, bases and
// tables, with a ring capture failure on every voter at one position, a policy change
// mid-run, a voter restarted mid-period and one installed mid-period; the installed
// voter answers the same bytes; after every voter restarts, every answer at or above
// the cut still delivers the store's changes, and the ring never drops an entry the
// durable layout does not cover. S5.3: apply seals past history writes still queued,
// an install waits for its peer's files, and a voter whose consensus log limits are
// below the group's policy refuses to start.
#include "change_history.hh"
#include "cluster_harness.hh"
#include "history.hh"
#include <iostream>
#include <map>

using namespace detent::authority;
using namespace kronuz::consensus;
using detent::authority::testing::Cluster;
namespace h = detent::authority::history;
namespace {
std::uint64_t sequence = 0, request = 100, fail_at = 0;
// The ring, failing its capture at one group revision.
struct Ring final : AppliedChangeRecorder {
	std::shared_ptr<ChangeHistory> inner = std::make_shared<ChangeHistory>(
		std::make_shared<detent::distribution::StorageLedger>(64 * 1024 * 1024), HistoryOptions{12, 4 * 1024 * 1024, 65536});
	unsigned failures = 0;
	void begin_batch(const Context &c, std::uint64_t i, std::uint64_t b) noexcept override { inner->begin_batch(c, i, b); }
	std::shared_ptr<AppliedChangeDraft> capture(const State &s, const Command &c,
												std::span<const unsigned char> changed) override {
		if (s.revision() == fail_at) {
			++failures;
			throw std::bad_alloc();
		}
		return inner->capture(s, c, changed);
	}
	void stage(std::shared_ptr<AppliedChangeDraft> d) noexcept override { inner->stage(std::move(d)); }
	void gap(std::uint64_t r) noexcept override { inner->gap(r); }
	void publish(const Context &c, std::uint64_t i, std::uint64_t r) noexcept override { inner->publish(c, i, r); }
	void abandon_batch() noexcept override { inner->abandon_batch(); }
	void reset(const Context &c, std::uint64_t r) noexcept override { inner->reset(c, r); }
	void covered(std::uint64_t r) noexcept override { inner->covered(r); }
	bool pending() const noexcept override { return inner->pending(); }
	void collect(CommandWorkBudget &b) noexcept override { inner->collect(b); }
};
std::array<std::shared_ptr<Ring>, 3> rings;
// Every applied write, by namespace, incarnation and position.
struct Write {
	Identity incarnation{};
	Revision position = 0;
	std::string key;
	std::optional<std::string> definition;
};
std::map<std::string, std::vector<Write>> writes;
std::map<std::string, Revision> gap_positions; // Where the capture failure hit, per namespace.

void commit(Cluster &group, std::string name, std::string key, std::optional<int> value) {
	Command c;
	c.context = group.context;
	c.producer = "writer";
	c.generation = 1;
	c.sequence = ++sequence;
	c.name_space = name;
	std::optional<detent::KnobDef> def;
	if (value)
		def = detent::KnobDef{*value};
	c.mutations.push_back({name + ":" + key, def, {}, {}});
	const auto expected = group.nodes[0]->state().revision() + 1;
	auto result = group.request(0, ++request, c).results.front();
	assert(result.status == Status::Applied || result.status == Status::NoChange);
	if (result.status != Status::Applied)
		return;
	const auto &state = group.nodes[0]->state();
	if (expected == fail_at)
		gap_positions[name] = result.namespace_revision;
	if (!state.positions_enabled())
		return;
	writes[name].push_back({state.namespace_entry(name)->incarnation, result.namespace_revision, name + ":" + key,
							def ? std::optional(encode_definition(*def)) : std::nullopt});
}
void settle(Cluster &group, std::source_location where = std::source_location::current()) {
	group.until([&] {
		return std::all_of(group.nodes.begin(), group.nodes.end(), [&](const auto &node) {
			return node->state().applied_index() == group.nodes[0]->state().applied_index() &&
				   !node->worker().busy() && node->state().applied_index() == node->worker().committed();
		});
	}, 20000, where);
}
std::map<std::string, std::string> files(Cluster &group, unsigned i) {
	std::map<std::string, std::string> out;
	h::Directory directory(group.history_io(i), {});
	for (const auto &name : directory.names())
		out[name] = directory.read(name);
	return out;
}
void identical(Cluster &group) {
	auto reference = files(group, 0);
	assert(reference.contains("layout"));
	for (const auto &[name, _] : reference)
		assert(!name.starts_with(".tmp-"));
	for (unsigned i = 1; i < 3; ++i)
		assert(files(group, i) == reference);
}
using Store = std::map<std::string, std::string>;
Store store_at(const std::string &name, const Identity &incarnation, Revision after) {
	Store s;
	for (const auto &w : writes[name])
		if (w.incarnation == incarnation && w.position <= after) {
			if (w.definition)
				s[w.key] = *w.definition;
			else
				s.erase(w.key);
		}
	return s;
}
struct Seen {
	h::Tier tier;
	std::vector<std::string> names;
	std::vector<h::Changes> parts;
	std::uint64_t sent;
};
// Every answer for namespace `name` on voter `i`: R from the cut to the head, each
// delivering exactly the head store; returns them by R.
std::map<Revision, Seen> answers(Cluster &group, unsigned i, const std::string &name, bool ring_tier) {
	const auto &state = group.nodes[i]->state();
	const auto *entry = state.namespace_entry(name);
	assert(entry && entry->live);
	const auto head = entry->position;
	const auto *history = group.nodes[i]->history();
	assert(history && history->ready());
	h::Directory directory(group.history_io(i), state.limits());
	const auto *n = history->layout().table().find(name);
	assert(n && n->incarnation == entry->incarnation);
	auto &ring = *rings[i]->inner;
	const auto floor = ring_tier ? ring.floor(name, entry->incarnation, head) : head;
	// The ring and the durable layout tile every position above the cut (lazy eviction).
	assert(!ring_tier || floor <= n->position || (gap_positions.contains(name) && floor <= gap_positions[name]));
	const auto target = store_at(name, entry->incarnation, head);
	std::map<Revision, Seen> out;
	for (Revision r = n->base.last; r <= head; ++r) {
		auto a = history->layout().answer(name, entry->incarnation, r, floor, directory);
		assert(a.tier != h::Tier::FullCopy);
		auto s = store_at(name, entry->incarnation, r);
		for (const auto &part : a.parts)
			for (const auto &c : part) {
				assert(c.revision > r && c.revision <= a.through);
				if (c.definition)
					s[c.key] = *c.definition;
				else
					s.erase(c.key);
			}
		// The ring serves (through, head] with each entry exactly once, by namespace position.
		if (ring_tier && a.through < floor) {
			assert(gap_positions.contains(name)); // Only the failed capture leaves a hole.
			continue;
		}
		if (ring_tier) {
			auto entries = ring.entries(name, entry->incarnation, a.through);
			assert(entries.size() == head - a.through);
			for (std::size_t k = 0; k < entries.size(); ++k)
				assert(entries[k]->position == a.through + 1 + k);
		}
		for (const auto &w : writes[name])
			if (w.incarnation == entry->incarnation && w.position > a.through && w.position <= head) {
				if (w.definition)
					s[w.key] = *w.definition;
				else
					s.erase(w.key);
			}
		assert(s == target);
		out[r] = {a.tier, a.names, a.parts, a.sent};
	}
	return out;
}
const char *names[] = {"a", "b", "c", "d", "e"};
void churn(Cluster &group, unsigned count, unsigned salt) {
	for (unsigned i = 0; i < count; ++i) {
		auto k = (i * 7 + salt) % 11;
		auto name = names[(i * 3 + salt) % 5];
		commit(group, name, "k" + std::to_string(k), (i + salt) % 7 == 0 ? std::nullopt : std::optional(int(i + salt)));
	}
}
} // namespace

int main() {
	LayoutPolicy policy;
	policy.seal_transactions = 6;
	policy.period = 4;
	policy.carry_bytes = 64;
	policy.carry_count = 2;
	policy.alpha_num = 1;
	policy.alpha_den = 2;
	testing::history_policy = policy;
	testing::memory_storage = true;
	testing::ring = [](unsigned i) { return rings[i] = std::make_shared<Ring>(); };
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
	churn(group, 12, 0); // A store written before the enabling entry.

	AdministrativeCommand enable{group.context, AdministrativeKind::EnablePositions, {}, {}, {}, {}};
	enable.nonce[0] = 's';
	assert(group.administer(0, 2, enable).results[0].status == Status::Applied);
	// Each enabled namespace starts at its first base; the oracle starts there too.
	for (auto name : names) {
		const auto &state = group.nodes[0]->state();
		state.for_each([&](const std::string &key, const Record &r) {
			if (key.starts_with(std::string(name) + ":"))
				writes[name].push_back({state.namespace_entry(name)->incarnation, r.revision, key,
										encode_definition(*r.value)});
		});
	}
	fail_at = group.nodes[0]->state().revision() + 9;
	churn(group, 40, 1);
	settle(group);
	for (const auto &r : rings)
		assert(r->failures == 1); // The same position failed on every voter.
	identical(group);

	// A policy change mid-run: a longer period, taking effect at a seal the old one divides.
	auto changed = policy;
	changed.period = 8;
	changed.seal_transactions = 5;
	AdministrativeCommand set{group.context, AdministrativeKind::SetLayout, {}, {}, {}, changed};
	assert(group.administer(0, 3, set).results[0].status == Status::Applied);
	churn(group, 30, 2);
	settle(group);
	identical(group);
	assert(group.nodes[0]->state().policy().period == 8);

	// Apply's seal returns before its history is written: voter 1 seals past held writes,
	// and its checkpoints and ring stay at the newest durable seal until they land.
	group.step(); // Settles the writes the last step drove.
	const auto durable = group.nodes[1]->durable_seal()->applied_index();
	assert(durable == group.nodes[1]->sealed()->applied_index());
	group.held_history[1] = true;
	for (unsigned salt = 20; group.nodes[1]->sealed()->applied_index() == durable; ++salt)
		churn(group, 1, salt);
	group.until([&] { return group.nodes[1]->sealed()->applied_index() > durable && bool(group.held[1]); });
	const auto pending = group.nodes[1]->sealed()->applied_index();
	assert(!files(group, 1).contains(h::seal_name(pending)) && !group.nodes[1]->history()->durable_through(pending) &&
		   group.nodes[1]->durable_seal()->applied_index() == durable);
	group.held_history[1] = false;
	group.release_history(1);
	settle(group);
	group.until([&] { return group.nodes[1]->durable_seal()->applied_index() == group.nodes[1]->sealed()->applied_index(); });
	identical(group);

	// Voter 2 restarts mid-period and replays above its checkpoint.
	while (group.nodes[2]->state().seal().ordinal % group.nodes[2]->state().policy().period == 0)
		churn(group, 1, 3);
	group.link(0, 2, false);
	group.link(1, 2, false);
	group.nodes[2].reset();
	group.messages[2].clear();
	churn(group, 15, 4);
	group.launch(2, true);
	group.link(0, 2, true);
	group.link(1, 2, true);
	churn(group, 10, 5);
	settle(group);
	identical(group);

	// Voter 1 is installed from a snapshot taken mid-period.
	group.link(0, 1, false);
	group.link(1, 2, false);
	const auto missed = group.nodes[1]->state().applied_index();
	churn(group, 20, 6);
	while (!group.nodes[0]->sealed() ||
		   group.nodes[0]->sealed()->seal().ordinal % group.nodes[0]->sealed()->policy().period == 0)
		churn(group, 1, 7);
	group.until([&] { return group.nodes[0]->worker().applied_index() == group.nodes[0]->state().applied_index(); });
	const auto snapshot = group.nodes[0]->sealed()->applied_index();
	assert(group.nodes[0]->checkpoint());
	group.until([&] { return !group.nodes[0]->checkpoint_busy(); });
	churn(group, 3, 8);
	assert(group.nodes[0]->worker().base_index() > missed);
	// The activation waits while the install's peer fetches are outstanding.
	group.deferred = true;
	group.link(0, 1, true);
	group.link(1, 2, true);
	group.until([&] { return !group.fetches.empty(); });
	for (unsigned k = 0; k < 50; ++k)
		group.step();
	assert(group.nodes[1]->history()->restoring() && group.nodes[1]->state().applied_index() < snapshot);
	group.until([&] {
		group.answer_fetches();
		return group.nodes[1]->state().applied_index() >= snapshot;
	});
	group.deferred = false;
	churn(group, 12, 9);
	settle(group);
	assert(group.nodes[1]->worker().base_index() >= snapshot);
	identical(group);

	// The installed voter answers the same bytes as its peers, block for block.
	for (auto name : names)
		for (unsigned i = 1; i < 3; ++i) {
			auto left = answers(group, 0, name, false), right = answers(group, i, name, false);
			assert(left.size() == right.size());
			for (const auto &[r, a] : left)
				assert(right[r].names == a.names && right[r].parts == a.parts && right[r].sent == a.sent);
		}
	std::map<std::string, std::map<Revision, Seen>> before;
	for (auto name : names)
		before[name] = answers(group, 0, name, true);

	// Every voter restarts: answers at or above the cut are unchanged, the ring refilled by replay.
	for (unsigned i = 0; i < 3; ++i) {
		group.nodes[i].reset();
		group.messages[i].clear();
	}
	for (unsigned i = 0; i < 3; ++i)
		group.launch(i, true);
	group.link(0, 1, true);
	group.link(0, 2, true);
	group.link(1, 2, true);
	group.until([&] {
		return std::any_of(group.nodes.begin(), group.nodes.end(),
						   [](const auto &node) { return node->worker().role() == Role::Leader; });
	});
	settle(group);
	identical(group);
	unsigned leader = 0;
	while (group.nodes[leader]->worker().role() != Role::Leader)
		++leader;
	std::size_t ring_answers = 0, kept = 0;
	for (auto name : names) {
		auto after = answers(group, leader, name, true);
		assert(after.size() == before[name].size());
		for (const auto &[r, a] : before[name]) {
			const auto &b = after[r];
			if (a.tier == h::Tier::Blocks)
				assert(b.tier == h::Tier::Blocks && b.names == a.names && b.parts == a.parts);
			ring_answers += a.tier == h::Tier::Ring;
			kept += a.tier == h::Tier::Ring && b.tier == h::Tier::Ring;
		}
	}
	// A voter whose consensus log limits are below the group's policy refuses to start (F6).
	const unsigned follower = leader == 2 ? 1 : 2;
	churn(group, 12, 10);
	settle(group);
	group.until([&] { // A checkpoint at its newest durable seal, its own or one it took itself.
		auto &node = *group.nodes[follower];
		if (!node.checkpoint_busy())
			node.checkpoint();
		return !node.checkpoint_busy() && node.worker().base_index() == node.durable_seal()->applied_index();
	});
	assert(group.nodes[follower]->state().positions_enabled());
	for (unsigned i = 0; i < 3; ++i)
		if (i != follower)
			group.link(follower, i, false);
	group.nodes[follower].reset();
	group.messages[follower].clear();
	testing::configure = [&](unsigned i, NodeOptions &o) {
		if (i == follower)
			o.consensus.log_entries = group.nodes[leader]->state().policy().log_entries / 2;
	};
	bool refused = false;
	try {
		group.launch(follower, true);
	} catch (const std::runtime_error &error) {
		refused = std::string_view(error.what()).find("consensus_limits") != std::string_view::npos;
	}
	assert(refused);
	testing::configure = {};
	group.nodes[follower].reset();
	group.launch(follower, true);
	for (unsigned i = 0; i < 3; ++i)
		if (i != follower)
			group.link(follower, i, true);
	settle(group);
	identical(group);

	const auto &state = group.nodes[leader]->state();
	std::cout << "seal " << state.seal().ordinal << " at index " << state.seal().index << ", "
			  << files(group, 0).size() << " history files identical on three voters; ring answers kept "
			  << kept << " of " << ring_answers << "\n";
}
