// A relay chain over real voters (namespace-delivery D7, W8, T4(1)): three voters write S5.2's
// history; relay A reads them through W5/W6 and their layout, a different voter each step; B reads
// A and C reads B, never a voter. At every step each relay holds every block, base and section
// byte-identical to every voter's, and answers every store cursor with the same W6 bytes as an
// oracle that replays every write. A restarted relay re-reads its ring and fetches nothing it holds.
#include "change_history.hh"
#include "cluster_harness.hh"
#include "detent_relay/namespace_relay.hh"
#include "history.hh"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <set>

using namespace detent::authority;
using namespace kronuz::consensus;
using detent::authority::testing::Cluster;
namespace h = detent::authority::history;
namespace d = detent::distribution;
namespace r = detent::relay;
namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("check failed at line " + std::to_string(line));
}
#define CHECK(x) check(bool(x), __LINE__)

// Every applied write, by namespace: its incarnation, position (a record's revision before the
// enabling entry) and value.
struct Write {
	Identity incarnation{};
	Revision position = 0;
	std::string key;
	std::optional<detent::KnobDef> value;
};
std::map<std::string, std::vector<Write>> writes;
std::uint64_t sequence = 0, request = 100;

void commit(Cluster &group, const std::string &name, const std::string &key, std::optional<int> value) {
	Command c;
	c.context = group.context;
	c.producer = "writer";
	c.generation = 1;
	c.sequence = ++sequence;
	c.name_space = name;
	std::optional<detent::KnobDef> def;
	if (value) {
		def = detent::KnobDef{*value};
		if (*value % 3 == 0)
			def->overrides.emplace("group-" + std::to_string(*value % 4), std::int64_t{*value * 7});
	}
	c.mutations.push_back({name + ":" + key, def, {}, {}});
	auto result = group.request(0, ++request, c).results.front();
	CHECK(result.status == Status::Applied || result.status == Status::NoChange);
	const auto &state = group.nodes[0]->state();
	if (result.status == Status::Applied && state.positions_enabled())
		writes[name].push_back({state.namespace_entry(name)->incarnation, result.namespace_revision, name + ":" + key, def});
}
void settle(Cluster &group) {
	group.until([&] {
		return std::all_of(group.nodes.begin(), group.nodes.end(), [&](const auto &node) {
			return node->state().applied_index() == group.nodes[0]->state().applied_index() &&
				   !node->worker().busy() && node->state().applied_index() == node->worker().committed();
		});
	});
}
const std::vector<std::string> names{"a", "b", "c", "d", "e"};
std::set<std::string> retired; // dropped and never written again
void churn(Cluster &group, unsigned count, unsigned salt) {
	for (unsigned i = 0; i < count; ++i)
		if (const auto &name = names[(i * 3 + salt) % 5]; !retired.contains(name))
			commit(group, name, "k" + std::to_string((i * 7 + salt) % 11),
				   (i + salt) % 7 == 0 ? std::nullopt : std::optional(int(i + salt)));
}

// The newest position of an incarnation: its last write, or s when none followed. A dropped
// incarnation ends at its last write; the drop's own position belongs to no write.
Revision head_of(const h::TableNamespace &n) {
	Revision head = n.position;
	for (const auto &w : writes[n.name])
		if (w.incarnation == n.incarnation)
			head = std::max(head, w.position);
	return head;
}
// Records in a reader's order.
using Row = std::tuple<std::string, Revision, std::optional<detent::KnobDef>>;
class Rows final : public d::HistoryReader {
  public:
	explicit Rows(std::vector<Row> rows) : rows_(std::move(rows)) {}
	const d::HistoryRecord *next() override {
		if (at_ == rows_.size())
			return nullptr;
		const auto &[key, revision, value] = rows_[at_++];
		record_ = {key, revision, value ? &*value : nullptr};
		return &record_;
	}

  private:
	std::vector<Row> rows_;
	std::size_t at_ = 0;
	d::HistoryRecord record_;
};
// The authority's history of one namespace, from the writes alone, tiled as the voter's layout.
class Oracle final : public d::NamespaceHistory {
  public:
	Oracle(const Identity &cluster, const h::TableNamespace &n) : cluster_(cluster), n_(n), head_(head_of(n)) {
		for (const auto &b : n.blocks)
			spans_.push_back({b.first, b.last, true});
		for (const auto &b : n.level0)
			spans_.push_back({b.first, b.last, false});
	}
	d::NamespaceCursor head() const noexcept override { return {cluster_, n_.incarnation, head_}; }
	std::uint64_t cut() const noexcept override { return n_.base.last; }
	std::uint64_t ring_start() const noexcept override { return n_.position; }
	std::span<const d::HistorySpan> spans() const noexcept override { return spans_; }
	std::unique_ptr<d::HistoryReader> read_span(std::size_t i) const override {
		return std::make_unique<Rows>(last_writes(spans_[i].first, spans_[i].last, false));
	}
	std::unique_ptr<d::HistoryReader> read_ring(std::uint64_t after) const override {
		std::vector<Row> rows;
		for (auto p = after + 1; p <= head_; ++p)
			for (auto &row : last_writes(p, p, false))
				rows.push_back(std::move(row));
		return std::make_unique<Rows>(std::move(rows));
	}
	std::unique_ptr<d::HistoryReader> read_full_copy() const override {
		return std::make_unique<Rows>(last_writes(0, n_.position, true));
	}

  private:
	std::vector<Row> last_writes(Revision first, Revision last, bool live) const {
		std::map<std::string, Row> out;
		for (const auto &w : writes[n_.name])
			if (w.incarnation == n_.incarnation && w.position >= first && w.position <= last)
				out[w.key] = {w.key, w.position, w.value};
		std::vector<Row> rows;
		for (auto &[key, row] : out)
			if (!live || std::get<2>(row))
				rows.push_back(std::move(row));
		return rows;
	}
	Identity cluster_;
	const h::TableNamespace &n_;
	Revision head_;
	std::vector<d::HistorySpan> spans_;
};

std::string slurp(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	CHECK(bool(in));
	return {std::istreambuf_iterator<char>(in), {}};
}
r::Digest digest_of(const Digest &digest) {
	r::Digest out;
	std::memcpy(out.data(), digest.data(), out.size());
	return out;
}
// A voter as the authority upstream: its layout table, its files by name (a section from its seal
// file) and W6 answers from the oracle tiled as its layout.
struct Voter final : r::Upstream {
	Cluster &group;
	unsigned index = 0;
	std::uint64_t fetched_files = 0, fetched_bytes = 0, frames_served = 0;
	explicit Voter(Cluster &group) : group(group) {}
	const h::Table &table() const { return group.nodes[index]->history()->layout().table(); }
	// A file of the voter's history; `seal-<index>/<block name>` is a section of that seal file.
	std::string read(const std::string &name) const {
		const auto slash = name.find('/');
		if (slash == std::string::npos)
			return slurp(group.history_directory(index) / name);
		const auto &limits = group.nodes[index]->state().limits();
		const auto seal = h::decode_seal(slurp(group.history_directory(index) / name.substr(0, slash)), limits);
		auto named = [&](const std::string &bytes) {
			const auto b = h::decode_block(bytes, limits);
			return h::block_name(b.name_space, b.incarnation, b.first, b.last) == name.substr(slash + 1);
		};
		for (const auto &n : seal.namespaces)
			if (n.section && named(*n.section))
				return *n.section;
		for (const auto &c : seal.carried)
			if (named(c))
				return c;
		throw std::runtime_error("no section " + name);
	}
	r::MirrorFile describe(r::MirrorFile::Kind kind, std::string name, const h::TableBlock &b) const {
		const auto bytes = read(name);
		CHECK(h::sha256(bytes) == b.digest);
		return {kind, std::move(name), b.first, b.last, bytes.size(), digest_of(b.digest)};
	}
	r::NamespaceLayout layout(const h::TableNamespace &n) const {
		using K = r::MirrorFile::Kind;
		const auto &t = table();
		r::NamespaceLayout out{n.name, n.incarnation, t.index, t.ordinal, n.base.last, encode_policy(t.policy), {}};
		if (n.base.bytes)
			out.files.push_back(describe(K::Base, h::base_name(n.name, n.incarnation, n.base.last), n.base));
		for (const auto &b : n.blocks)
			out.files.push_back(describe(K::Block, h::block_name(n.name, n.incarnation, b.first, b.last), b));
		for (const auto &b : n.level0)
			out.files.push_back(describe(
				K::Section, h::seal_name(b.seal) + "/" + h::block_name(n.name, n.incarnation, b.first, b.last), b));
		return out;
	}
	std::vector<r::NamespaceLayout> layouts(std::span<const std::string> wanted) override {
		std::vector<r::NamespaceLayout> out;
		for (const auto &name : wanted)
			if (const auto *n = table().find(name))
				out.push_back(layout(*n));
		return out;
	}
	std::string fetch(const r::MirrorFile &file) override {
		auto bytes = read(file.name);
		++fetched_files;
		fetched_bytes += bytes.size();
		return bytes;
	}
	std::string frames(std::span<const r::Position> positions) override {
		++frames_served;
		std::vector<std::unique_ptr<Oracle>> histories;
		std::vector<d::NamespaceRequest> requests;
		for (const auto &p : positions)
			if (const auto *n = table().find(p.name)) {
				histories.push_back(std::make_unique<Oracle>(group.context.cluster, *n));
				requests.push_back({p.name, histories.back().get(), p.cursor});
			}
		return d::encode_session_frames(std::nullopt, requests);
	}
};
// Counts what a downstream relay reads from its upstream relay; files can be refused.
struct Counted final : r::Upstream {
	r::Upstream *inner;
	std::uint64_t fetched_files = 0;
	bool files = true;
	explicit Counted(r::Upstream &inner) : inner(&inner) {}
	std::vector<r::NamespaceLayout> layouts(std::span<const std::string> n) override { return inner->layouts(n); }
	std::string fetch(const r::MirrorFile &f) override {
		CHECK(files);
		++fetched_files;
		return inner->fetch(f);
	}
	std::string frames(std::span<const r::Position> p) override { return inner->frames(p); }
};

std::filesystem::path prepare(const std::filesystem::path &root) {
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	std::filesystem::permissions(root, std::filesystem::perms::owner_all);
	return root;
}
std::shared_ptr<kronuz::io::completion::PosixIO> io_at(const std::filesystem::path &root) {
	return std::make_shared<kronuz::io::completion::PosixIO>(root, 0644);
}

// Every store cursor for `name`: none, another incarnation, and each R from 0 to h + 1.
std::vector<std::optional<d::NamespaceCursor>> cursors(const Identity &cluster, const Identity &incarnation, Revision head) {
	std::vector<std::optional<d::NamespaceCursor>> out{std::nullopt};
	auto other = incarnation;
	other[0] ^= 1;
	out.push_back(d::NamespaceCursor{cluster, other, head});
	for (Revision at = 0; at <= head + 1; ++at)
		out.push_back(d::NamespaceCursor{cluster, incarnation, at});
	return out;
}
struct Totals {
	std::uint64_t files = 0, answers = 0, answer_bytes = 0, steps = 0;
};
// A1: every relay holds every file of every voter's layout under the same name and bytes. A5: every
// relay answers every store cursor with the oracle's W6 bytes.
void compare(Cluster &group, Voter &voter, std::span<r::NamespaceRelay *const> relays,
			 std::span<const std::filesystem::path> roots, Totals &totals) {
	const auto reference = voter.index;
	for (unsigned i = 0; i < 3; ++i) {
		voter.index = i;
		const auto layouts = voter.layouts(names);
		for (std::size_t k = 0; k < relays.size(); ++k) {
			CHECK(relays[k]->mirror().table().entries == layouts);
			for (const auto &layout : layouts)
				for (const auto &file : layout.files) {
					CHECK(slurp(roots[k] / r::mirror_file_name(file.name)) == voter.read(file.name));
					++totals.files;
				}
		}
	}
	voter.index = reference;
	const auto served = voter.frames_served;
	for (const auto &layout : voter.layouts(names)) {
		const auto head = head_of(*voter.table().find(layout.name));
		for (auto *relay : relays)
			CHECK(relay->head(layout.name) == d::NamespaceCursor(group.context.cluster, layout.incarnation, head));
		for (const auto &cursor : cursors(group.context.cluster, layout.incarnation, head)) {
			const r::Position position{layout.name, cursor};
			const auto expected = voter.frames({&position, 1});
			for (auto *relay : relays)
				CHECK(relay->frames({&position, 1}) == expected);
			++totals.answers;
			totals.answer_bytes += expected.size();
		}
	}
	voter.frames_served = served;
}
} // namespace

int main(int argc, char **argv) {
	try {
		CHECK(argc == 2);
		const std::filesystem::path scratch = argv[1];
		LayoutPolicy policy;
		policy.seal_transactions = 6;
		policy.period = 4;
		policy.carry_bytes = 64;
		policy.carry_count = 2;
		policy.alpha_num = 1;
		policy.alpha_den = 2;
		testing::history_policy = policy;
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
		churn(group, 12, 0); // a store written before the enabling entry
		AdministrativeCommand enable{group.context, AdministrativeKind::EnablePositions, {}, {}, {}, {}};
		enable.nonce[0] = 's';
		CHECK(group.administer(0, 2, enable).results[0].status == Status::Applied);
		for (const auto &name : names) {
			const auto &state = group.nodes[0]->state();
			state.for_each([&](const std::string &key, const Record &record) {
				if (key.starts_with(name + ":"))
					writes[name].push_back({state.namespace_entry(name)->incarnation, record.revision, key, *record.value});
			});
		}
		settle(group);

		const std::array<std::filesystem::path, 3> roots{prepare(scratch / "relay-a"), prepare(scratch / "relay-b"),
														 prepare(scratch / "relay-c")};
		Voter voter(group);
		auto a = std::make_unique<r::NamespaceRelay>(io_at(roots[0]), group.context.cluster, voter);
		Counted from_a(*a);
		auto b = std::make_unique<r::NamespaceRelay>(io_at(roots[1]), group.context.cluster, from_a);
		Counted from_b(*b);
		auto c = std::make_unique<r::NamespaceRelay>(io_at(roots[2]), group.context.cluster, from_b);
		Totals totals;
		std::uint64_t policy_changes = 0, drops = 0;
		std::set<Identity> incarnations_of_c;
		bool empty_base = false;
		for (unsigned step = 0; step < 14; ++step) {
			churn(group, 7, step + 1);
			if (step == 4) { // a policy change mid-run
				auto changed = policy;
				changed.period = 8;
				changed.seal_transactions = 5;
				AdministrativeCommand set{group.context, AdministrativeKind::SetLayout, {}, {}, {}, changed};
				CHECK(group.administer(0, 3, set).results[0].status == Status::Applied);
				++policy_changes;
			}
			if (step == 10) { // a drop for good: every relay drops the name
				const auto incarnation = group.nodes[0]->state().namespace_entry("e")->incarnation;
				AdministrativeCommand drop{group.context, AdministrativeKind::DropNamespace, {}, "e", incarnation, {}};
				CHECK(group.administer(0, 5, drop).results[0].status == Status::Applied);
				retired.insert("e");
				++drops;
			}
			if (step == 7) { // a drop; the next write re-creates the name under a new incarnation
				const auto incarnation = group.nodes[0]->state().namespace_entry("c")->incarnation;
				AdministrativeCommand drop{group.context, AdministrativeKind::DropNamespace, {}, "c", incarnation, {}};
				CHECK(group.administer(0, 4, drop).results[0].status == Status::Applied);
				++drops;
				// Relays sync at the first seal of the re-creation, while its base is empty (no file).
				commit(group, "c", "k0", 5);
				for (int k = 0; [&] {
						 const auto *n = group.nodes[0]->history()->layout().table().find("c");
						 return !n || n->incarnation == incarnation;
					 }();)
					commit(group, "a", "k1", ++k);
			}
			settle(group);
			voter.index = step % 3;
			a->sync(names);
			b->sync(names);
			c->sync(names);
			r::NamespaceRelay *relays[] = {a.get(), b.get(), c.get()};
			compare(group, voter, relays, roots, totals);
			++totals.steps;
			for (const auto &e : c->mirror().table().entries) {
				empty_base = empty_base || e.files.empty() || e.files.front().kind != r::MirrorFile::Kind::Base;
				if (e.name == "c")
					incarnations_of_c.insert(e.incarnation);
			}
		}
		CHECK(group.nodes[0]->state().policy().period == 8 && policy_changes == 1 && drops == 2);
		CHECK(incarnations_of_c.size() == 2 && empty_base); // the re-creation, its empty base mirrored
		for (auto *relay : {a.get(), b.get(), c.get()})
			CHECK(!relay->mirror().table().find("e") && !relay->head("e"));

		// A2: C and A restart; their rings re-read from upstream, every answer is unchanged and no
		// file they hold is fetched again.
		c.reset();
		from_b.files = false;
		c = std::make_unique<r::NamespaceRelay>(io_at(roots[2]), group.context.cluster, from_b);
		const auto c_sync = c->sync(names);
		CHECK(c_sync.files.fetched_files == 0 && c_sync.files.reused_files > 0);
		const auto voter_files = voter.fetched_files;
		a.reset();
		a = std::make_unique<r::NamespaceRelay>(io_at(roots[0]), group.context.cluster, voter);
		from_a.inner = a.get();
		const auto a_sync = a->sync(names);
		CHECK(a_sync.files.fetched_files == 0 && voter.fetched_files == voter_files);
		r::NamespaceRelay *relays[] = {a.get(), b.get(), c.get()};
		compare(group, voter, relays, roots, totals);
		// The next seal: A fetches only the new names, and C reads them from B.
		from_b.files = true;
		churn(group, 9, 40);
		settle(group);
		a->sync(names);
		b->sync(names);
		c->sync(names);
		compare(group, voter, relays, roots, totals);
		std::cout << "relay chain: " << totals.steps << " steps over 3 voters and 3 relays, " << totals.files
				  << " file comparisons byte-identical, " << totals.answers << " cursors answered with identical W6 bytes ("
				  << totals.answer_bytes << " bytes per hop); voter files fetched " << voter.fetched_files << " ("
				  << voter.fetched_bytes << " bytes); B served " << from_a.fetched_files << ", C restarted with "
				  << c_sync.entries << " ring positions re-read and 0 files fetched\n";
	} catch (const std::exception &error) {
		std::cerr << "test_relay_chain: " << error.what() << "\n";
		return 1;
	}
	return 0;
}
