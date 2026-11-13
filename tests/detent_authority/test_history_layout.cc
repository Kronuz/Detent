// namespace-delivery D6: seals in apply, the layout's roll-ups, squash and cut,
// and the three answers, against an oracle that keeps every position's state.
#include "history_layout.hh"
#include <bit>
#include <cassert>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

using namespace detent::authority;
namespace h = detent::authority::history;
namespace {
Context context() {
	Context c;
	c.cluster[0] = 'c';
	c.group[0] = 'g';
	c.epoch[0] = 'e';
	return c;
}
template <class F> bool throws(F &&f) {
	try {
		f();
	} catch (const std::exception &) {
		return true;
	}
	return false;
}
struct MemoryFiles final : h::Files {
	std::map<std::string, std::string, std::less<>> files;
	std::string read(std::string_view name) override {
		auto found = files.find(name);
		if (found == files.end())
			throw std::runtime_error("absent history file " + std::string(name));
		return found->second;
	}
	void store(const h::Step &step) {
		files[h::seal_name(step.seal.index)] = step.bytes;
		for (const auto &[name, bytes] : step.written)
			files[name] = bytes;
	}
};
// A namespace's state at a position: key to (revision, canonical definition).
using Image = std::map<std::string, std::pair<Revision, std::string>>;
Image image_of(const State &s, const std::string &name) {
	Image out;
	s.for_each([&](const std::string &key, const Record &r) {
		if (key.substr(0, key.find(':')) == name)
			out[key] = {r.revision, encode_definition(*r.value)};
	});
	return out;
}
void apply(Image &store, const h::Changes &changes) {
	for (const auto &c : changes)
		if (c.definition)
			store[c.key] = {c.revision, *c.definition};
		else
			store.erase(c.key);
}
std::uint64_t image_bytes(const Image &image) {
	std::uint64_t total = 0;
	for (const auto &[key, value] : image)
		total += key.size() + 16 + value.second.size();
	return total;
}

// Drives a State through transactions and log entries, sealing as apply does.
struct Run {
	State state;
	std::uint64_t index = 0, sequence = 0;
	h::Layout layout;
	MemoryFiles files;
	std::vector<h::Step> steps;
	std::map<std::string, std::map<Revision, Image>> oracle; // name -> position -> state
	std::map<std::string, Identity> incarnations;
	// Bytes sent per answer tier, against the missing changes (A6).
	struct Sent {
		std::uint64_t ring = 0, blocks = 0, copies = 0, block_bytes = 0, missing = 0, block_read = 0, copy_bytes = 0;
		double worst = 0;
	} sent;
	// The full check answers every R, so it runs at power-of-two seals, every 32nd and at the end.
	~Run() {
		if (!steps.empty() && !std::has_single_bit(steps.size()) && steps.size() % 32 != 0)
			check();
	}
	explicit Run(LayoutPolicy policy, Limits limits = {}) : state(context(), limits), layout(limits) {
		Command open;
		open.context = context();
		open.kind = Kind::Open;
		open.producer = "p";
		open.nonce[0] = 'n';
		state.apply(++index, open);
		entry(0);
		AdministrativeCommand enable{context(), AdministrativeKind::EnablePositions, {}, {}, {}, policy};
		enable.nonce[0] = 's';
		assert(state.administer(++index, enable).status == Status::Applied);
		assert(state.seal().forced);
		entry(64);
		assert(steps.size() == 1 && steps[0].table && steps[0].seal.ordinal == 0);
	}
	bool write(const std::string &name, const std::string &key, std::optional<int> value, unsigned overrides = 0) {
		Command c;
		c.context = context();
		c.producer = "p";
		c.generation = 1;
		c.sequence = ++sequence;
		c.name_space = name;
		std::optional<detent::KnobDef> def;
		if (value) {
			def.emplace(detent::KnobDef{*value});
			for (unsigned i = 0; i < overrides; ++i)
				def->overrides.emplace("g" + std::to_string(i), std::int64_t{i});
		}
		c.mutations.push_back({name + ":" + key, def, {}, {}});
		auto result = state.apply(++index, c);
		if (result.status == Status::Applied) {
			auto entry = state.namespace_entry(name);
			if (!incarnations.count(name) || incarnations[name] != entry->incarnation) {
				incarnations[name] = entry->incarnation;
				oracle[name].clear();
				oracle[name][entry->origin] = {}; // A new incarnation starts empty.
			}
			oracle[name][result.namespace_revision] = image_of(state, name);
		}
		entry(100 + key.size());
		return result.status == Status::Applied;
	}
	void noop() {
		state.advance(++index);
		entry(0);
	}
	void entry(std::uint64_t bytes) {
		if (!state.close_entry(bytes))
			return;
		auto step = layout.seal(state, files);
		// The seal file lists exactly the outputs its seal writes.
		std::vector<std::string> listed, written;
		for (const auto &o : step.seal.outputs)
			listed.push_back(o.name());
		for (const auto &[name, _] : step.written)
			written.push_back(name);
		assert(listed == written && h::decode_seal(step.bytes, Limits{}).outputs == step.seal.outputs);
		files.store(step);
		if (!step.seal.ordinal)
			for (const auto &n : step.seal.namespaces) {
				incarnations[n.name] = n.incarnation;
				oracle[n.name][n.position] = image_of(state, n.name);
			}
		steps.push_back(std::move(step));
		if (std::has_single_bit(steps.size()) || steps.size() % 32 == 0)
			check();
	}
	// Every answer for every R reaches st(s) with st(s)'s revisions; the tier follows the cut.
	void check() {
		for (const auto &n : layout.table().namespaces) {
			const auto &history = oracle.at(n.name);
			const auto &head = history.at(n.position);
			assert(image_bytes(head) == n.live);
			for (const auto &[at, store] : history) {
				if (at > n.position)
					break;
				auto a = layout.answer(n.name, n.incarnation, at, ~Revision{0}, files);
				Image result = a.tier == h::Tier::FullCopy ? Image{} : store;
				for (const auto &part : a.parts)
					apply(result, part);
				assert(result == head);
				if (at < n.base.last) {
					assert(a.tier == h::Tier::FullCopy && a.sent == n.live && a.parts.size() == 1);
					++sent.copies;
					sent.copy_bytes += a.sent;
				} else if (at < n.position) {
					assert(a.tier == h::Tier::Blocks && a.sent <= a.read);
					std::uint64_t missing = 0; // Each key changed after R, once, at its last write.
					for (const auto &[key, value] : head)
						if (value.first > at)
							missing += key.size() + 16 + value.second.size();
					assert(a.sent >= missing);
					++sent.blocks;
					sent.block_bytes += a.sent;
					sent.block_read += a.read;
					sent.missing += missing;
					if (missing)
						sent.worst = std::max(sent.worst, double(a.sent) / double(missing));
				} else {
					assert(a.tier == h::Tier::Ring);
					++sent.ring;
				}
			}
			// Another incarnation, or a store below the base, gets the full copy.
			Identity other = n.incarnation;
			other[0] ^= 1;
			auto copy = layout.answer(n.name, other, n.position, ~Revision{0}, files);
			assert(copy.tier == h::Tier::FullCopy && copy.sent == n.live);
			auto ring = layout.answer(n.name, n.incarnation, n.base.last, n.base.last, files);
			assert(ring.tier == h::Tier::Ring && ring.parts.empty());
		}
	}
};

void seal_trigger() {
	LayoutPolicy policy;
	policy.seal_transactions = 3;
	policy.log_entries = 80; // A seal every 10 entries.
	Run run(policy);
	assert(run.state.seal().ordinal == 0 && run.state.seal().index == run.index);
	run.write("a", "x", 1);
	run.write("a", "y", 2);
	assert(run.state.pending_changes() == 2 && run.steps.size() == 1);
	run.write("b", "x", 3); // The third positioned change seals.
	assert(run.steps.size() == 2 && run.state.seal().ordinal == 1 && run.state.pending_changes() == 0);
	assert(run.steps[1].seal.namespaces.size() == 2 && run.steps[1].seal.previous == run.steps[0].seal.index);
	// Log progress seals entries that get no position: NoChange and refusals.
	for (int i = 0; i < 9; ++i)
		run.write("a", "x", 1); // NoChange.
	assert(run.steps.size() == 2);
	run.write("a", "x", 1);
	assert(run.steps.size() == 3 && run.steps[2].seal.namespaces.empty());
	for (int i = 0; i < 10; ++i)
		run.noop(); // Leader no-ops count too.
	assert(run.steps.size() == 4);
	// Canonical bytes seal: one 8-byte budget fills at once.
	AdministrativeCommand set{context(), AdministrativeKind::SetLayout, {}, {}, {}, policy};
	set.policy.seal_bytes = 8;
	assert(run.state.administer(++run.index, set).status == Status::Applied);
	assert(run.state.pending_policy() && run.state.policy().seal_bytes != 8);
	run.entry(64);
	assert(run.state.administer(++run.index, set).status == Status::NoChange);
	run.entry(64);
	const auto seals = run.steps.size();
	for (int i = 0; i < 10 && run.steps.size() == seals; ++i)
		run.noop();
	assert(run.state.policy().seal_bytes == 8 && !run.state.pending_policy());
	assert(run.steps.back().seal.policy.seal_bytes == 8);
	run.write("b", "y", 4);
	assert(run.steps.back().seal.namespaces.size() == 1);
	// The image carries the counters, the policies and the accumulator.
	set.policy.seal_bytes = 1 << 20;
	assert(run.state.administer(++run.index, set).status == Status::Applied);
	run.entry(64);
	for (int i = 0; i < 10 && run.state.pending_policy(); ++i)
		run.noop();
	assert(run.state.policy().seal_bytes == 1 << 20);
	run.write("a", "z", 5);
	assert(run.state.pending_changes() == 1);
	auto restored = State::restore(run.state.checkpoint(), context());
	assert(restored.seal() == run.state.seal() && restored.policy() == run.state.policy() &&
		   restored.pending_policy() == run.state.pending_policy() && restored.pending_changes() == 1 &&
		   restored.checkpoint() == run.state.checkpoint() &&
		   restored.namespace_entry("a")->origin == run.state.namespace_entry("a")->origin);
	// An invalid policy is refused by the codec.
	AdministrativeCommand bad = set;
	bad.policy.alpha_den = 0;
	assert(throws([&] { encode(bad); }));
	assert(throws([&] { decode_policy(std::string(88, '\0')); }));
	assert(decode_policy(encode_policy(policy)) == policy);
}

void period_change() {
	// A new period takes effect only at a seal whose ordinal the old period divides.
	LayoutPolicy policy;
	policy.period = 4;
	policy.log_entries = 8; // A seal every entry.
	Run run(policy);
	run.noop(); // Ordinal 1.
	AdministrativeCommand set{context(), AdministrativeKind::SetLayout, {}, {}, {}, policy};
	set.policy.period = 3;
	set.policy.carry_count = 0;
	assert(run.state.administer(++run.index, set).status == Status::Applied);
	run.noop(); // Ordinal 2: the carry count applies, the period waits.
	assert(run.state.policy().carry_count == 0 && run.state.policy().period == 4 && run.state.pending_policy());
	run.noop(); // Ordinal 3.
	assert(run.state.policy().period == 4);
	run.noop(); // Ordinal 4: 4 divides it.
	assert(run.state.seal().ordinal == 4 && run.state.policy().period == 3 && !run.state.pending_policy());
	assert(!run.steps.back().table); // 3 does not divide 4.
	run.noop();
	run.noop(); // Ordinal 6.
	assert(run.steps.back().table && run.steps.back().table->ordinal == 6);
}

// An independent model of one namespace's layout (the design's rules, as the
// key-level model writes them), compared with the table after every seal.
struct Model {
	struct Piece {
		Revision first, last;
		std::map<std::string, std::uint64_t> sizes; // key -> canonical size (a tombstone's too)
		std::map<std::string, bool> tombstone;
		std::uint64_t bytes() const {
			std::uint64_t t = 0;
			for (auto &[k, s] : sizes)
				t += s;
			return t;
		}
	};
	static Piece join(const Piece &older, const Piece &younger) {
		Piece out{older.first, younger.last, older.sizes, older.tombstone};
		for (auto &[k, s] : younger.sizes) {
			out.sizes[k] = s;
			out.tombstone[k] = younger.tombstone.at(k);
		}
		return out;
	}
	LayoutPolicy policy;
	std::vector<Piece> level0, blocks;
	Piece base{0, 0, {}, {}};
	std::uint64_t carries = 0;
	bool touched = false;
	void seal(std::uint64_t ordinal, std::uint64_t directory, std::uint64_t live, std::optional<Piece> section) {
		if (section) {
			level0.push_back(*section);
			touched = true;
		}
		if (level0.empty())
			return;
		auto limit = static_cast<std::uint64_t>((unsigned __int128)live * policy.alpha_num / (4 * policy.alpha_den));
		std::uint64_t held = 0;
		for (auto &p : level0)
			held += p.bytes();
		bool periodic = (ordinal + directory) % policy.period == 0;
		if (!periodic && held < limit)
			return;
		Piece rolled = level0.front();
		for (std::size_t i = 1; i < level0.size(); ++i)
			rolled = join(rolled, level0[i]);
		bool was = touched;
		touched = false;
		level0.clear();
		if (periodic && rolled.bytes() < std::min(policy.carry_bytes, limit) && was && carries < policy.carry_count) {
			++carries;
			level0.push_back(rolled);
			return;
		}
		carries = 0;
		blocks.push_back(rolled);
		while (blocks.size() >= 2) {
			auto &o = blocks[blocks.size() - 2], &y = blocks.back();
			if (o.bytes() >= policy.ratio * y.bytes())
				break;
			auto m = join(o, y);
			if (m.bytes() > limit && m.bytes() > std::max(o.bytes(), y.bytes()))
				break;
			blocks.pop_back();
			blocks.back() = m;
		}
		while (blocks.size() >= 2) {
			std::uint64_t younger = 0;
			for (std::size_t i = 1; i < blocks.size(); ++i)
				younger += blocks[i].bytes();
			unsigned __int128 budget = (unsigned __int128)policy.alpha_num * live;
			if (!((unsigned __int128)younger * policy.alpha_den >= budget ||
				  (unsigned __int128)(younger + blocks[0].bytes()) * policy.alpha_den > budget * policy.cap))
				break;
			auto oldest = blocks.front();
			blocks.erase(blocks.begin());
			for (auto &[k, s] : oldest.sizes)
				if (oldest.tombstone[k]) {
					base.sizes.erase(k);
					base.tombstone.erase(k);
				} else {
					base.sizes[k] = s;
					base.tombstone[k] = false;
				}
			base.last = oldest.last;
		}
	}
	void compare(const h::TableNamespace &n) const {
		assert(n.base.last == base.last && n.base.bytes == base.bytes() && n.carries == carries &&
			   n.touched == touched && n.blocks.size() == blocks.size() && n.level0.size() == level0.size());
		for (std::size_t i = 0; i < blocks.size(); ++i)
			assert(n.blocks[i].first == blocks[i].first && n.blocks[i].last == blocks[i].last &&
				   n.blocks[i].bytes == blocks[i].bytes());
		for (std::size_t i = 0; i < level0.size(); ++i)
			assert(n.level0[i].first == level0[i].first && n.level0[i].last == level0[i].last &&
				   n.level0[i].bytes == level0[i].bytes() && n.level0[i].carried == (carries && i == 0));
	}
};

// Random churn over a few namespaces: writes, deletes, re-inserts, NoChange, a drop and a re-creation.
void churn(std::uint64_t seed, LayoutPolicy policy) {
	std::mt19937_64 random(seed);
	Run run(policy);
	const std::vector<std::string> names{"a", "b", "c", "d"};
	std::map<std::string, Model> models;
	std::map<std::string, Revision> sealed_at; // The model's s per namespace.
	std::size_t seen = 1;
	std::uint64_t carried = 0, cost = 0, cap = 0;
	auto follow = [&] {
		for (; seen < run.steps.size(); ++seen) {
			const auto &step = run.steps[seen];
			std::set<std::string> changed;
			for (const auto &n : step.seal.namespaces) {
				changed.insert(n.name);
				if (n.dropped) {
					models.erase(n.name);
					continue;
				}
				auto [it, fresh] = models.try_emplace(n.name);
				if (fresh || !sealed_at.count(n.name)) {
					it->second = Model{step.seal.policy, {}, {}, {0, n.origin, {}, {}}, 0, false};
					sealed_at[n.name] = n.origin;
				}
				auto section = h::decode_block(*n.section, Limits{});
				Model::Piece piece{section.first, section.last, {}, {}};
				for (const auto &c : section.changes) {
					piece.sizes[c.key] = c.size();
					piece.tombstone[c.key] = !c.definition;
				}
				it->second.policy = step.seal.policy;
				it->second.seal(step.seal.ordinal, n.directory, n.live, piece);
				sealed_at[n.name] = n.position;
			}
			for (auto &[name, model] : models)
				if (!changed.count(name)) {
					auto *t = run.layout.table().find(name);
					model.policy = step.seal.policy;
					model.seal(step.seal.ordinal, t->directory, t->live, std::nullopt);
				}
			carried += step.seal.carried.size();
			for (auto &[name, model] : models)
				model.compare(*run.layout.table().find(name));
		}
	};
	for (int i = 0; i < 3000; ++i) {
		auto name = names[random() % (i < 1500 ? 2 : names.size())];
		auto hot = random() % 4 != 0;
		auto key = "k" + std::to_string(hot ? random() % 6 : random() % 60);
		auto roll = random() % 10;
		if (roll < 2)
			run.write(name, key, std::nullopt);
		else
			run.write(name, key, static_cast<int>(random() % (roll == 2 ? 2 : 1000)), random() % 3);
		if (i == 2000) {
			// Drop "b"; its next write re-creates it under a new incarnation.
			auto entry = run.state.namespace_entry("b");
			AdministrativeCommand drop{context(), AdministrativeKind::DropNamespace, {}, "b", entry->incarnation};
			assert(run.state.administer(++run.index, drop).status == Status::Applied);
			run.oracle.erase("b");
			run.incarnations.erase("b");
			sealed_at.erase("b");
			run.entry(64);
		}
		follow();
	}
	for (const auto &[name, counts] : run.layout.cuts()) {
		cost += counts[0];
		cap += counts[1];
	}
	// Replaying the seal files from the first table reaches every table.
	h::Layout replica(run.steps[0].table.value());
	for (std::size_t i = 1; i < run.steps.size(); ++i) {
		auto step = replica.replay(run.steps[i].bytes, run.files);
		assert(step.seal == run.steps[i].seal && step.written == run.steps[i].written &&
			   step.table == run.steps[i].table);
	}
	assert(replica.table() == run.layout.table());
	// A seal file with a flipped byte, or one whose outputs were edited, is refused.
	auto rotten = run.steps.back().bytes;
	rotten[rotten.size() / 2] ^= 1;
	assert(throws([&] { h::decode_seal(rotten, Limits{}); }));
	std::cout << "seed " << seed << " period " << policy.period << " alpha " << policy.alpha_num << "/"
			  << policy.alpha_den << ": " << run.steps.size() << " seals, " << cost << " cost cuts, " << cap
			  << " cap cuts, " << carried << " carried sections; answers: " << run.sent.ring << " ring, "
			  << run.sent.blocks << " blocks sending " << run.sent.block_bytes << " bytes (read "
			  << run.sent.block_read << ") for " << run.sent.missing << " missing, worst "
			  << run.sent.worst << "x, " << run.sent.copies << " full copies sending " << run.sent.copy_bytes
			  << " bytes\n";
	assert(run.sent.ring && run.sent.blocks && run.sent.copies);
	assert(cost > 0 && run.steps.size() > 50);
}

void formats() {
	h::Changes older{{"a:x", 1, "d1"}, {"a:y", 2, std::nullopt}};
	h::Changes younger{{"a:y", 3, "d3"}, {"a:z", 4, std::nullopt}};
	auto merged = h::merge(older, younger);
	assert((merged == h::Changes{{"a:x", 1, "d1"}, {"a:y", 3, "d3"}, {"a:z", 4, std::nullopt}}));
	assert((h::merge(older, younger, true) == h::Changes{{"a:x", 1, "d1"}, {"a:y", 3, "d3"}}));
	assert((h::trim(merged, 2) == h::Changes{{"a:y", 3, "d3"}, {"a:z", 4, std::nullopt}}));
	auto def = encode_definition(detent::KnobDef{7});
	h::Block b{"a", {}, 1, 4, {{"a:x", 1, def}, {"a:y", 4, std::nullopt}}};
	auto bytes = h::encode_block(b);
	assert(h::decode_block(bytes, Limits{}) == b && h::bytes(b.changes) == 3 + 16 + def.size() + 3 + 16);
	assert(throws([&] { h::decode_block(bytes.substr(0, bytes.size() - 1), Limits{}); }));
	h::Block unordered = b;
	std::swap(unordered.changes[0], unordered.changes[1]);
	assert(throws([&] { h::decode_block(h::encode_block(unordered), Limits{}); }));
	h::Block outside = b;
	outside.changes[1].revision = 5; // Past the block's last position.
	assert(throws([&] { h::decode_block(h::encode_block(outside), Limits{}); }));
	assert(throws([&] { h::encode_base(b); })); // A base holds no tombstone.
	assert(h::block_name("a", {}, 1, 4) == "block-ca978112ca1bbdcafac231b39a23dc4d-00000000000000000000000000000000-1-4" &&
		   h::seal_name(255) == "seal-00000000000000ff");
}
} // namespace

int main() {
	formats();
	seal_trigger();
	period_change();
	LayoutPolicy small; // A small group: seals every 16 transactions, short periods.
	small.seal_transactions = 16;
	small.period = 4;
	small.carry_bytes = 600;
	churn(1, small);
	small.alpha_num = 1;
	small.alpha_den = 1;
	churn(2, small);
	LayoutPolicy chosen; // D90's α and period, at a seal per 16 transactions.
	chosen.seal_transactions = 16;
	churn(3, chosen);
	chosen.carry_count = 0;
	churn(4, chosen);
	std::cout << "history layout: seals, roll-ups, squash, cuts and answers match the oracle\n";
}
