// S5.1: per-namespace positions, incarnations, live sizes, the directory and
// the enabling entry on one State (docs/status/namespace-s5.md).
#include "namespace_oracle.hh"
#include "persistent_map.hh"
#include "state.hh"
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <string>

using namespace detent;
using namespace detent::authority;
namespace {
Context context() {
	Context c;
	c.cluster[0] = 'c';
	c.group[0] = 'g';
	c.epoch[0] = 'e';
	return c;
}
Identity nonce(char value = 'n') {
	Identity id{};
	id[0] = value;
	return id;
}
Command open() {
	Command c;
	c.context = context();
	c.kind = Kind::Open;
	c.producer = "writer";
	c.nonce[0] = 'a';
	return c;
}
Mutation put(std::string key, std::int64_t value) { return {std::move(key), KnobDef{value}, {}, {}}; }
Mutation erase(std::string key) { return {std::move(key), std::nullopt, {}, {}}; }
Command transaction(std::uint64_t sequence, std::string name, std::vector<Mutation> operations) {
	Command c;
	c.context = context();
	c.producer = "writer";
	c.generation = 1;
	c.sequence = sequence;
	c.name_space = std::move(name);
	c.mutations = std::move(operations);
	return c;
}
AdministrativeCommand enable(char value = 'n') { return {context(), AdministrativeKind::EnablePositions, nonce(value), {}}; }
// Drops the incarnation the name has now (any nonzero one before positions exist).
AdministrativeCommand drop(const State &state, std::string name) {
	auto entry = state.namespace_entry(name);
	auto incarnation = entry && entry->incarnation != Identity{} ? entry->incarnation : nonce('x');
	return {context(), AdministrativeKind::DropNamespace, {}, std::move(name), incarnation};
}
std::uint64_t footprint(std::string_view key) { return key.size() + 9 + 16; } // A definition with a default only.

// Applies through the stepped path the node runs, with a deliberately small budget.
Result apply_stepped(State &state, std::uint64_t index, const Command &command) {
	auto bytes = encode(command);
	CommandDecoder decoder(bytes, state.limits(), state.context());
	while (!decoder.done()) {
		CommandWorkBudget budget;
		decoder.step(budget);
	}
	CommandApplication application(state, index, 1, decoder.take());
	while (!application.done()) {
		CommandWorkBudget budget{2048, 1, 1, 1};
		application.step(budget);
	}
	auto outcome = application.finish();
	state = std::move(outcome.state);
	return outcome.results.front();
}
Result administer_stepped(State &state, std::uint64_t index, const AdministrativeCommand &command,
						  unsigned &steps) {
	AdministrativeApplication application(state, index, 1, command);
	while (!application.done()) {
		CommandWorkBudget budget{1024, 1, 1, 1};
		application.step(budget);
		++steps;
	}
	auto outcome = application.finish();
	state = std::move(outcome.state);
	return outcome.results.front();
}
// The namespace fields and records, then the whole image byte for byte.
void same(const State &a, const State &b) {
	testing::same_namespaces(a, b);
	assert(a.checkpoint() == b.checkpoint());
}

void positions_start_at_stored_revision() {
	State s(context());
	std::uint64_t index = 0;
	s.apply(++index, open());
	assert(s.apply(++index, transaction(1, "a", {put("a:x", 1)})).revision == 1);
	assert(s.apply(++index, transaction(2, "b", {put("b:x", 1)})).revision == 2);
	auto third = s.apply(++index, transaction(3, "a", {put("a:y", 1)}));
	assert(third.revision == 3 && third.namespace_revision == 3);
	// Before the enabling entry: the stored revision is the group's, and sizes are kept already.
	assert(!s.positions_enabled() && s.namespace_entry("a")->incarnation == Identity{} &&
		   s.namespace_entry("a")->directory == 0 && s.directory_position() == 0);
	assert(s.namespace_entry("a")->bytes == footprint("a:x") + footprint("a:y") &&
		   s.namespace_entry("b")->bytes == footprint("b:x") &&
		   s.logical_bytes() == s.namespace_entry("a")->bytes + s.namespace_entry("b")->bytes);

	auto enabled = s.administer(++index, enable());
	assert(enabled.status == Status::Applied && s.positions_enabled() && s.revision() == 3);
	auto a = *s.namespace_entry("a"), b = *s.namespace_entry("b");
	assert(a.position == 3 && b.position == 2);					   // Each starts at its stored revision.
	assert(a.directory == 1 && b.directory == 2 && s.directory_position() == 2); // In name order.
	assert(a.incarnation == derive_incarnation(context(), nonce(), 1, "a") &&
		   b.incarnation == derive_incarnation(context(), nonce(), 2, "b") && a.incarnation != b.incarnation &&
		   a.incarnation != Identity{});
	assert(s.find("a:x")->revision == 1 && s.find("a:y")->revision == 3); // Records keep their revisions.

	// A change advances its namespace only; the group revision still counts every change.
	auto written = s.apply(++index, transaction(4, "b", {put("b:y", 2)}));
	assert(written.status == Status::Applied && written.revision == 4 && written.namespace_revision == 3);
	assert(s.find("b:y")->revision == 3 && s.namespace_entry("a")->position == 3 &&
		   s.namespace_entry("b")->bytes == footprint("b:x") + footprint("b:y"));
	// A namespace created later starts at position 0 and takes the next directory position.
	auto created = s.apply(++index, transaction(5, "c", {put("c:x", 1)}));
	assert(created.namespace_revision == 1 && s.namespace_entry("c")->directory == 3 &&
		   s.directory_position() == 3 &&
		   s.namespace_entry("c")->incarnation == derive_incarnation(context(), nonce(), 3, "c"));
	// No position for NoChange or refusals.
	assert(s.apply(++index, transaction(6, "a", {put("a:x", 1)})).status == Status::NoChange);
	auto conflict = transaction(7, "a", {put("a:z", 1)});
	conflict.mutations[0].expected = 99;
	assert(s.apply(++index, conflict).status == Status::Conflict);
	assert(s.apply(++index, transaction(9, "a", {put("a:z", 1)})).status == Status::SequenceGap);
	assert(s.namespace_entry("a")->position == 3 && s.revision() == 5);
	// Preconditions compare namespace positions.
	auto conditional = transaction(8, "a", {erase("a:x")});
	conditional.expected_namespace = 3;
	assert(s.apply(++index, conditional).namespace_revision == 4 && s.namespace_entry("a")->position == 4 &&
		   s.namespace_entry("a")->bytes == footprint("a:y"));
	auto keyed = transaction(9, "b", {put("b:y", 3)});
	keyed.mutations[0].expected = 3; // b:y's revision is b's position 3, not the group's 4.
	assert(s.apply(++index, keyed).status == Status::Applied && s.find("b:y")->revision == 4);

	// A second enabling entry changes nothing.
	auto image = s.checkpoint();
	assert(s.administer(++index, enable('z')).status == Status::NoChange && s.checkpoint().substr(8 + 48 + 80 + 8 + 8) == image.substr(8 + 48 + 80 + 8 + 8));

	// Restart: the image carries positions, directory and seed; sizes are recomputed from records.
	auto restored = State::restore(s.checkpoint(), context());
	same(s, restored);
	auto next = transaction(10, "a", {put("a:w", 5)});
	++index;
	assert(s.apply(index, next) == restored.apply(index, next));
	same(s, restored);
	assert(s.namespace_entry("a")->position == 5);
}

void stepped_and_synchronous_agree() {
	State sync(context()), stepped(context());
	std::uint64_t index = 0;
	std::vector<Command> commands;
	for (std::uint64_t i = 1; i <= 40; ++i) {
		auto name = std::string("n") + char('a' + i % 7);
		commands.push_back(transaction(i, name, {put(name + ":k" + std::to_string(i % 5), std::int64_t(i)),
												 put(name + ":j", std::int64_t(i % 3))}));
	}
	sync.apply(++index, open());
	stepped.apply(index, open());
	unsigned steps = 0;
	for (std::size_t i = 0; i < commands.size(); ++i) {
		if (i == 17) {
			++index;
			assert(sync.administer(index, enable()) == administer_stepped(stepped, index, enable(), steps));
			assert(steps > 7); // One namespace per step.
		}
		++index;
		assert(sync.apply(index, commands[i]) == apply_stepped(stepped, index, commands[i]));
	}
	same(sync, stepped);
	++index;
	assert(sync.administer(index, drop(sync, "nc")) == administer_stepped(stepped, index, drop(sync, "nc"), steps));
	same(sync, stepped);
}

void directory_stream() {
	State s(context());
	std::uint64_t index = 0;
	s.apply(++index, open());
	s.apply(++index, transaction(1, "b", {put("b:x", 1)}));
	s.apply(++index, transaction(2, "a", {put("a:x", 1), put("a:y", 2)}));
	s.administer(++index, enable());
	auto list = [&](std::uint64_t after) {
		std::vector<std::tuple<std::uint64_t, std::string, Revision, Identity, std::uint64_t, bool>> rows;
		s.for_each_directory_entry(after, [&](auto d, auto &name, auto &e) {
			rows.emplace_back(d, name, e.position, e.incarnation, e.bytes, e.live);
		});
		return rows;
	};
	auto all = list(0);
	assert(all.size() == 2 && std::get<1>(all[0]) == "a" && std::get<2>(all[0]) == 2 &&
		   std::get<4>(all[0]) == footprint("a:x") + footprint("a:y") && std::get<1>(all[1]) == "b" &&
		   std::get<2>(all[1]) == 1 && std::get<4>(all[1]) == footprint("b:x"));
	assert(list(s.directory_position()).empty());
	s.apply(++index, transaction(3, "c", {put("c:x", 1)}));
	auto suffix = list(2);
	assert(suffix.size() == 1 && std::get<0>(suffix[0]) == 3 && std::get<1>(suffix[0]) == "c" &&
		   std::get<3>(suffix[0]) == s.namespace_entry("c")->incarnation);
	// Writes in a namespace move its position, not its directory entry.
	s.apply(++index, transaction(4, "a", {put("a:z", 1)}));
	assert(list(3).empty() && std::get<2>(list(0)[0]) == 3);
	// A drop moves the entry to the head as a tombstone; one entry per name, always.
	s.administer(++index, drop(s, "b"));
	auto after = list(3);
	assert(after.size() == 1 && std::get<0>(after[0]) == 4 && std::get<1>(after[0]) == "b" &&
		   !std::get<5>(after[0]) && std::get<4>(after[0]) == 0 && list(0).size() == 3);
}

void incarnations_and_drop() {
	State s(context());
	std::uint64_t index = 0;
	s.apply(++index, open());
	s.apply(++index, transaction(1, "a", {put("a:x", 1), put("a:y", 2)}));
	assert(s.administer(++index, drop(s, "a")).status == Status::Conflict); // Positions are not on yet.
	// Neighbours share the name as a prefix; the drop removes exactly "a:".
	s.apply(++index, transaction(2, "a.b", {put("a.b:x", 1)}));
	s.apply(++index, transaction(3, "ab", {put("ab:x", 1)}));
	s.administer(++index, enable());
	assert(s.administer(++index, drop(s, "zz")).status == Status::NoChange);
	auto old = s.namespace_cursor("a");
	assert(old.position == 1 && s.answer("a", old) == CursorAnswer::Current);
	auto held = s; // A reader holding the pre-drop root, as an export capture does.
	auto bytes = s.logical_bytes();
	auto dropped = s.administer(++index, drop(s, "a"));
	assert(dropped.status == Status::Applied && dropped.namespace_revision == 2 && dropped.revision == 4);
	auto tomb = *s.namespace_entry("a");
	assert(!tomb.live && tomb.position == 2 && tomb.bytes == 0 && tomb.incarnation == old.incarnation &&
		   tomb.directory == 4 && s.directory_position() == 4);
	assert(!s.find("a:x") && !s.find("a:y") && s.find("a.b:x") && s.find("ab:x") && s.size() == 2 &&
		   s.logical_bytes() == bytes - footprint("a:x") - footprint("a:y"));
	assert(s.answer("a", old) == CursorAnswer::Absent);
	assert(s.administer(++index, drop(s, "a")).status == Status::NoChange);
	// The held root still reads the dropped namespace whole.
	assert(held.find("a:x") && held.find("a:y") && held.namespace_entry("a")->live);

	// An in-flight write that observed the old incarnation conflicts; positions are never reused.
	auto stale = transaction(4, "a", {put("a:x", 7)});
	stale.expected_namespace = old.position;
	assert(s.apply(++index, stale).status == Status::Conflict);
	auto absent = transaction(5, "a", {{"a:x", KnobDef{7}, 0, old.position}});
	assert(s.apply(++index, absent).status == Status::Conflict);
	// An unconditional write re-creates the name under a new incarnation.
	auto recreated = s.apply(++index, transaction(6, "a", {put("a:x", 8)}));
	assert(recreated.status == Status::Applied && recreated.namespace_revision == 3);
	auto now = *s.namespace_entry("a");
	assert(now.live && now.directory == 5 && now.incarnation != old.incarnation &&
		   now.incarnation == derive_incarnation(context(), nonce(), 5, "a") && now.bytes == footprint("a:x"));
	// The old cursor gets a full copy (W7); the new one is answered by position.
	assert(s.answer("a", old) == CursorAnswer::FullCopy);
	// A retried drop of the old incarnation leaves the re-creation alone.
	AdministrativeCommand retried{context(), AdministrativeKind::DropNamespace, {}, "a", old.incarnation};
	assert(s.administer(++index, retried).status == Status::Conflict && s.namespace_entry("a")->live &&
		   s.find("a:x"));
	auto cursor = s.namespace_cursor("a");
	assert(s.answer("a", cursor) == CursorAnswer::Current);
	assert(s.answer("a", {cursor.cluster, cursor.incarnation, 0}) == CursorAnswer::Behind);
	assert(s.answer("a", {cursor.cluster, cursor.incarnation, 4}) == CursorAnswer::Ahead);
	assert(s.answer("a", {Identity{}, cursor.incarnation, 3}) == CursorAnswer::FullCopy);
	assert(s.answer("a", {cursor.cluster, Identity{}, 3}) == CursorAnswer::FullCopy); // A pre-S5 store.
	assert(s.answer("never", cursor) == CursorAnswer::Absent);

	// Tombstones and re-creations survive the image.
	s.administer(++index, drop(s, "ab"));
	auto restored = State::restore(s.checkpoint(), context());
	same(s, restored);
	assert(!restored.namespace_entry("ab")->live && restored.answer("a", cursor) == CursorAnswer::Current);

	// Reclaim: retiring the held root frees the dropped records in bounded steps.
	auto retained = held.retained_nodes();
	held.retire();
	unsigned turns = 0;
	while (held.reclamation_pending()) {
		CommandWorkBudget budget{2048, 16, 8, 1};
		held.collect(budget);
		assert(++turns < 10000);
	}
	assert(turns > 1 && retained > 0 && s.find("a.b:x") && s.find("a:x"));
}

// The range removal keeps the AVL invariants and matches an ordered map.
void erase_range_matches_map() {
	std::mt19937_64 random(7);
	for (unsigned round = 0; round < 200; ++round) {
		detail::PersistentMap<std::string, int> map;
		std::map<std::string, int> oracle;
		auto keys = 1 + random() % 300;
		for (unsigned i = 0; i < keys; ++i) {
			auto key = std::to_string(random() % 1000);
			map = map.set(key, int(i));
			oracle[key] = int(i);
		}
		auto lo = std::to_string(random() % 1000), hi = std::to_string(random() % 1000);
		if (hi < lo)
			std::swap(lo, hi);
		auto held = map;
		map = map.erase_range(lo, hi);
		oracle.erase(oracle.lower_bound(lo), oracle.lower_bound(hi));
		std::vector<std::pair<std::string, int>> rows;
		map.for_each([&](const std::string &k, int v) { rows.emplace_back(k, v); });
		assert((rows == std::vector<std::pair<std::string, int>>(oracle.begin(), oracle.end())));
		assert(map.size() == oracle.size());
		auto n = double(oracle.size() + 2);
		assert(map.height() <= int(1.45 * std::log2(n)) + 1);
		std::size_t before = 0;
		held.for_each([&](const std::string &, int) { ++before; });
		assert(before == held.size()); // The source root is untouched.
	}
}
} // namespace
int main() {
	positions_start_at_stored_revision();
	stepped_and_synchronous_agree();
	directory_stream();
	incarnations_and_drop();
	erase_range_matches_map();
	std::cout << "namespace positions, incarnations, sizes, directory and enabling entry passed\n";
}
