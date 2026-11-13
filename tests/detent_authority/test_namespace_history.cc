// A1 (namespace-s6-cutover): a voter answers W6 from S5.2's history. Three voters churn five
// namespaces across seals, roll-ups, squashes and cuts, a drop and a re-creation, definitions with
// overrides and rollouts, and a ring capture failure at one position on every voter. For every
// namespace and every store cursor (none, another incarnation, each R from 0 to h + 1), each voter's
// AuthorityHistory answers with the same bytes; the tier follows the cut and the ring start; the
// blocks and full copies carry exactly S5.2's own answer (`Layout::answer`) record for record; the
// decoded body brings the store at R to the head the voter reports. While the capture gap is above
// the latest seal, the head stops at the gap (review F5 of namespace-s5-2) and the answer is still
// exact; the next seal covers it.
#include "change_history.hh"
#include "cluster_harness.hh"
#include "detent_distribution/session_frames.hh"
#include "history.hh"
#include "namespace_history.hh"
#include <iostream>
#include <map>

using namespace detent::authority;
using namespace kronuz::consensus;
using detent::authority::testing::Cluster;
namespace d = detent::distribution;
namespace h = detent::authority::history;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value) {
		std::cerr << "namespace history invariant at line " << where.line() << "\n";
		std::abort();
	}
}
std::uint64_t sequence = 0, request = 100, fail_at = 0;
struct Ring final : AppliedChangeRecorder {
	std::shared_ptr<ChangeHistory> inner = std::make_shared<ChangeHistory>(
		std::make_shared<d::StorageLedger>(64 * 1024 * 1024), HistoryOptions{256, 4 * 1024 * 1024, 65536});
	void begin_batch(const Context &c, std::uint64_t i, std::uint64_t b) noexcept override { inner->begin_batch(c, i, b); }
	std::shared_ptr<AppliedChangeDraft> capture(const State &s, const Command &c,
												std::span<const unsigned char> changed) override {
		if (s.revision() == fail_at)
			throw std::bad_alloc();
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
struct Write {
	Identity incarnation{};
	Revision position = 0;
	std::string key;
	std::optional<std::string> definition;
};
std::map<std::string, std::vector<Write>> writes;
using Store = std::map<std::string, std::pair<Revision, std::string>>;
Store store_at(const std::string &name, const Identity &incarnation, Revision after) {
	Store s;
	for (const auto &w : writes[name])
		if (w.incarnation == incarnation && w.position <= after) {
			if (w.definition)
				s[w.key] = {w.position, *w.definition};
			else
				s.erase(w.key);
		}
	return s;
}
detent::KnobDef definition(int value) {
	detent::KnobDef def{value};
	if (value % 3 == 0) {
		def.overrides.emplace("g" + std::to_string(value % 5), std::int64_t{-value});
		def.overrides.emplace("h", detent::GroupRollout{static_cast<std::uint8_t>(value % 101)});
		def.rollout_value = value * 7; // A group rollout ramps the knob.
	}
	if (value % 4 == 0) {
		def.rollout_pct = static_cast<std::uint8_t>(value % 100);
		def.rollout_value = value * 7;
	}
	return def;
}
void commit(Cluster &group, const std::string &name, std::vector<std::pair<std::string, std::optional<int>>> ops) {
	Command c;
	c.context = group.context;
	c.producer = "writer";
	c.generation = 1;
	c.sequence = ++sequence;
	c.name_space = name;
	for (auto &[key, value] : ops)
		c.mutations.push_back({name + ":" + key, value ? std::optional(definition(*value)) : std::nullopt, {}, {}});
	auto result = group.request(0, ++request, c).results.front();
	check(result.status == Status::Applied || result.status == Status::NoChange);
	const auto &state = group.nodes[0]->state();
	if (result.status != Status::Applied || !state.positions_enabled())
		return;
	for (const auto &m : c.mutations) {
		const auto *record = state.find(m.key);
		if (m.value && (!record || record->revision != result.namespace_revision))
			continue; // An unchanged value.
		writes[name].push_back({state.namespace_entry(name)->incarnation, result.namespace_revision, m.key,
								m.value ? std::optional(encode_definition(*m.value)) : std::nullopt});
	}
}
const char *names[] = {"a", "b", "c", "d.e", "d.f"};
void churn(Cluster &group, unsigned count, unsigned salt) {
	for (unsigned i = 0; i < count; ++i) {
		const auto name = names[(i * 3 + salt) % 5];
		std::vector<std::pair<std::string, std::optional<int>>> ops;
		for (unsigned k = 0; k < 1 + (i + salt) % 3; ++k) {
			const auto key = (i * 7 + salt + k * 5) % 11;
			ops.emplace_back("k" + std::to_string(key),
							 (i + salt + k) % 7 == 0 ? std::nullopt : std::optional(int(i * 13 + salt + k)));
		}
		std::sort(ops.begin(), ops.end());
		ops.erase(std::unique(ops.begin(), ops.end(), [](auto &x, auto &y) { return x.first == y.first; }), ops.end());
		std::reverse(ops.begin(), ops.end()); // The ring sorts what the producer sends in any order.
		commit(group, name, std::move(ops));
	}
}
void settle(Cluster &group) {
	group.until([&] {
		return std::all_of(group.nodes.begin(), group.nodes.end(), [&](const auto &node) {
			return node->state().applied_index() == group.nodes[0]->state().applied_index() &&
				   !node->worker().busy() && node->state().applied_index() == node->worker().committed();
		});
	});
	group.step(); // Settles the history writes the last step drove.
}
// The decoded body: every frame's pieces and records, in order.
struct Piece {
	d::PieceKind kind;
	std::uint64_t first, last;
	std::vector<std::tuple<std::string, Revision, std::optional<std::string>>> records;
};
struct Decoded final : d::FrameSink {
	std::optional<d::FrameKind> kind;
	std::uint64_t after = 0;
	d::NamespaceCursor to;
	std::vector<Piece> pieces;
	unsigned frames = 0;
	void frame(std::string_view, d::FrameKind k, std::uint64_t a, const d::NamespaceCursor &t) override {
		++frames;
		kind = k;
		after = a;
		to = t;
	}
	void piece(d::PieceKind k, std::uint64_t first, std::uint64_t last) override { pieces.push_back({k, first, last, {}}); }
	void record(std::string_view key, std::uint64_t revision, const detent::KnobDef *value) override {
		pieces.back().records.emplace_back(std::string(key), revision,
										   value ? std::optional(encode_definition(*value)) : std::nullopt);
	}
};
using Records = std::vector<std::tuple<std::string, Revision, std::optional<std::string>>>;
Records flatten(const std::vector<h::Changes> &parts) {
	Records out;
	for (const auto &part : parts)
		for (const auto &c : part)
			out.emplace_back(c.key, c.revision, c.definition);
	return out;
}
Records flatten(const std::vector<Piece> &pieces) {
	Records out;
	for (const auto &piece : pieces)
		out.insert(out.end(), piece.records.begin(), piece.records.end());
	return out;
}
struct Totals {
	std::uint64_t answers = 0, bytes = 0, full = 0, blocks = 0, entries = 0, current = 0, held = 0;
};
std::unique_ptr<AuthorityHistory> freeze(Cluster &group, unsigned i, const std::string &name) {
	auto &node = *group.nodes[i];
	check(node.history() && node.history()->ready() && node.sealed());
	return AuthorityHistory::freeze(name, group.context.cluster, node.state(), *node.sealed(),
									node.history()->layout().table(), *node.history_files(), *rings[i]->inner);
}
// Every cursor of every namespace on every voter; returns what was answered.
Totals answer_all(Cluster &group) {
	Totals totals;
	for (auto name : names) {
		const auto *entry = group.nodes[0]->state().namespace_entry(name);
		if (!entry || !entry->live) {
			for (unsigned i = 0; i < 3; ++i)
				check(!freeze(group, i, name)); // Nothing answers a dropped namespace.
			continue;
		}
		std::array<std::unique_ptr<AuthorityHistory>, 3> histories;
		for (unsigned i = 0; i < 3; ++i)
			histories[i] = freeze(group, i, name);
		const auto &history = *histories[0];
		const auto head = history.head();
		check(head.incarnation == entry->incarnation && head.position + history.held_back() == entry->position);
		totals.held += history.held_back() > 0;
		const auto *layout = group.nodes[0]->history()->layout().table().find(name);
		auto other = head;
		other.incarnation[15] ^= 1;
		std::vector<std::optional<d::NamespaceCursor>> stores{std::nullopt, other};
		for (Revision r = 0; r <= head.position + 1; ++r)
			stores.push_back(d::NamespaceCursor{head.cluster, head.incarnation, r});
		for (const auto &store : stores) {
			std::string bytes;
			for (unsigned i = 0; i < 3; ++i) {
				d::NamespaceRequest one{name, histories[i].get(), store};
				auto answer = d::encode_session_frames(std::nullopt, {&one, 1});
				check(i == 0 ? (bytes = answer, true) : answer == bytes); // Byte-identical on every voter.
			}
			Decoded decoded;
			d::SessionFramesDecoder decoder(decoded);
			std::span<const char> input(bytes.data(), bytes.size());
			std::size_t budget = bytes.size();
			decoder.feed(input, budget);
			decoder.finish();
			++totals.answers;
			totals.bytes += bytes.size();
			const bool same = store && store->same_identity(head);
			const auto r = same ? store->position : 0;
			if (same && r >= head.position) {
				check(!decoded.frames); // Current, or Ahead: no frame.
				++totals.current;
				continue;
			}
			check(decoded.frames == 1 && decoded.to == head);
			// A full copy replaces the store; blocks and entries apply over it.
			Store s = same && decoded.kind != d::FrameKind::FullCopy ? store_at(name, head.incarnation, r) : Store{};
			// The tier the cut and the ring start choose (D6), checked against S5.2's own answer.
			if (!same || r < history.cut()) {
				check(decoded.kind == d::FrameKind::FullCopy && decoded.after == 0);
				check(decoded.pieces.front().kind == d::PieceKind::FullCopy &&
					  decoded.pieces.front().last == history.ring_start());
				if (layout && layout->incarnation == head.incarnation) {
					h::Directory files(group.history_io(0), group.nodes[0]->state().limits());
					auto a = group.nodes[0]->history()->layout().answer(name, other.incarnation, 0, UINT64_MAX, files);
					check(a.tier == h::Tier::FullCopy && a.parts.size() == 1 &&
						  flatten(a.parts) == decoded.pieces.front().records);
				} else
					check(decoded.pieces.front().records.empty());
				++totals.full;
			} else if (r < history.ring_start()) {
				check(decoded.kind == d::FrameKind::Blocks && decoded.after == r);
				h::Directory files(group.history_io(0), group.nodes[0]->state().limits());
				auto a = group.nodes[0]->history()->layout().answer(name, head.incarnation, r, UINT64_MAX, files);
				check(a.tier == h::Tier::Blocks && a.through == history.ring_start());
				std::vector<Piece> stored;
				for (const auto &piece : decoded.pieces)
					if (piece.kind != d::PieceKind::Entry)
						stored.push_back(piece);
				check(flatten(a.parts) == flatten(stored));
				++totals.blocks;
			} else {
				check(decoded.kind == d::FrameKind::Entries && decoded.after == r);
				for (const auto &piece : decoded.pieces)
					check(piece.kind == d::PieceKind::Entry && piece.first == piece.last);
				++totals.entries;
			}
			for (const auto &piece : decoded.pieces)
				for (const auto &[key, revision, value] : piece.records) {
					if (value)
						s[key] = {revision, *value};
					else
						s.erase(key); // A tombstone for a key the store lacks is a no-op (D8).
				}
			if (s != store_at(name, head.incarnation, head.position)) {
				std::cerr << name << " r=" << r << " same=" << same << " kind=" << int(*decoded.kind) << " cut="
						  << history.cut() << " s=" << history.ring_start() << " head=" << head.position << "\n";
				for (auto &[k, v] : s)
					std::cerr << " got " << k << "@" << v.first << "\n";
				for (auto &[k, v] : store_at(name, head.incarnation, head.position))
					std::cerr << " want " << k << "@" << v.first << "\n";
				for (auto &p : decoded.pieces) {
					std::cerr << " piece " << int(p.kind) << " [" << p.first << "," << p.last << "]";
					for (auto &[k, rv, v] : p.records)
						std::cerr << " " << k << "@" << rv << (v ? "" : "x");
					std::cerr << "\n";
				}
			}
			check(s == store_at(name, head.incarnation, head.position));
		}
	}
	return totals;
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
	check(group.administer(0, 2, enable).results[0].status == Status::Applied);
	for (auto name : names) {
		const auto &state = group.nodes[0]->state();
		state.for_each([&](const std::string &key, const Record &r) {
			if (key.starts_with(std::string(name) + ":"))
				writes[name].push_back({state.namespace_entry(name)->incarnation, r.revision, key,
										encode_definition(*r.value)});
		});
	}
	settle(group);
	auto totals = answer_all(group);

	// A capture failure on every voter: the head stops at the gap until a seal covers it.
	fail_at = group.nodes[0]->state().revision() + 1;
	const auto seals = group.nodes[0]->state().seal().ordinal;
	commit(group, "a", {{"gap", 1}});
	commit(group, "a", {{"after", 2}});
	settle(group);
	if (group.nodes[0]->state().seal().ordinal == seals) {
		auto held = freeze(group, 0, "a");
		check(held->held_back() == 2 && held->head().position + 2 == group.nodes[0]->state().namespace_entry("a")->position);
		totals.held += answer_all(group).held;
	}
	churn(group, 60, 1);
	settle(group);
	auto churned = answer_all(group);
	check(churned.full && churned.blocks && churned.entries);

	// A drop and a re-creation: the new incarnation starts where the old one ended.
	const auto *c = group.nodes[0]->state().namespace_entry("c");
	AdministrativeCommand drop{group.context, AdministrativeKind::DropNamespace, {}, "c", c->incarnation, {}};
	check(group.administer(0, ++request, drop).results[0].status == Status::Applied);
	settle(group);
	answer_all(group);
	commit(group, "c", {{"k1", 5}, {"k2", 6}});
	settle(group);
	answer_all(group);
	churn(group, 40, 2);
	settle(group);
	auto last = answer_all(group);
	check(totals.held > 0 || churned.held > 0);
	std::cout << last.answers << " cursors answered byte-identically on three voters (" << last.bytes
			  << " bytes): " << last.full << " full copies, " << last.blocks << " blocks, " << last.entries
			  << " entries, " << last.current << " without a frame; a capture gap held the head back in "
			  << totals.held << " answers\n";
}
