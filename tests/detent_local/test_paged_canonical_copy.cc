// Dense distribution DD2: relocating a generation that took deltas into seal order gives the full
// build's files, with no page digest computed. Usage: [rounds] or --corpus BEFORE AFTER.
#include "dense_corpus.hh"
#include "detent_local/paged_definition.hh"
#include "paged_canonical_copy.hh"
#include "paged_navigation.hh"
#include "paged_overlay_assembler.hh"
#include "paged_packer.hh"
#include "paged_read_state.hh"
#include "paged_segment_builder.hh"
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
using namespace dense_test;
namespace x = l::detail;
namespace d = detent::distribution;
using Records = std::map<std::string, std::vector<char>>;
using Changes = std::vector<std::pair<std::string, std::optional<std::vector<char>>>>;

static void run(x::SegmentBuilder &segment) {
	l::ValidationBudget budget{std::numeric_limits<std::size_t>::max(),
							   std::numeric_limits<std::size_t>::max()};
	segment.step(budget);
}
static double seconds(std::chrono::steady_clock::time_point begin) {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
}
static l::GenerationId generation(char fill) {
	l::GenerationId id;
	id.fill(fill);
	return id;
}
// A generation: its segments in ordinal order, and the id-sorted inputs reads search.
struct Store {
	p::Root root = empty_root();
	p::Limits limits;
	std::vector<x::BuiltSegment> segments;
	std::vector<x::SegmentInput> inputs;
	std::vector<x::SegmentReachability> live;
	std::uint64_t pack_bytes = 0;
	void index() {
		inputs.clear();
		pack_bytes = 0;
		for (const auto &s : segments) {
			inputs.push_back({s.ownership, {s.bytes->data(), s.bytes->size()}});
			pack_bytes += s.bytes->size();
		}
		std::sort(inputs.begin(), inputs.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(),
							   a.ownership.segment.size()) < 0;
		});
		live.assign(inputs.size(), {});
		if (!root.pages)
			return;
		std::vector<p::Reference> stack{root.tree};
		while (!stack.empty()) {
			const auto r = stack.back();
			stack.pop_back();
			const auto i = x::find_segment(inputs, r.segment);
			require(i != inputs.size());
			const auto bytes = x::checked_page_bytes(inputs[i], r);
			++live[i].pages;
			live[i].bytes += r.length;
			const auto h = p::decode_page_header(bytes, limits);
			for (std::uint16_t e = 0; e < h.externals; ++e)
				stack.push_back(p::decode_external(bytes, e, limits));
		}
	}
};
static p::Limits limits_for(const Records &rows, const Records &other = {}) {
	std::uint64_t records[2] = {};
	for (const auto *set : {&rows, &other})
		for (const auto &[key, body] : *set)
			records[set == &other] += key.front() == 'K';
	return corpus_limits(std::max(records[0], records[1]),
						 std::max(rows.size() - records[0], other.size() - records[1]));
}
// The full build: the packer's pages written as the snapshot builder writes them.
static std::shared_ptr<Store> build(const std::shared_ptr<d::StorageLedger> &account,
									const Records &rows, const l::GenerationId &id,
									const p::Limits &limits) {
	auto store = std::make_shared<Store>();
	store->limits = limits;
	x::PagedPacker packer(account, limits);
	std::optional<x::SegmentBuilder> segment;
	const auto close = [&] {
		segment->seal();
		run(*segment);
		store->segments.push_back({segment->ownership(), segment->bytes()});
		segment.reset();
	};
	const auto drain = [&] {
		for (unsigned turn = 0; !packer.accepted() && !packer.done(); ++turn) {
			require(turn < 100000000);
			l::ValidationBudget budget;
			packer.step(budget);
			if (!packer.ready())
				continue;
			const auto page = packer.page();
			if (segment && !segment->fits(page))
				close();
			if (!segment)
				segment.emplace(account, id, store->segments.size());
			segment->append(page);
			run(*segment);
			const auto r = segment->reference();
			++store->root.pages;
			store->root.bytes += r.length;
			for (unsigned commit = 0;; ++commit) {
				require(commit < 1000000);
				l::ValidationBudget b;
				if (packer.commit(r, b))
					break;
			}
		}
	};
	std::vector<Records::const_iterator> indexed;
	for (auto i = rows.begin(); i != rows.end(); ++i) {
		indexed.push_back(i);
		if (i->first.front() == 'K')
			++store->root.records;
		else
			++store->root.namespaces;
	}
	const auto node = [&](auto &&self, std::size_t lo, std::size_t hi, std::size_t start) -> void {
		const auto &first = indexed[lo]->first;
		const auto &last = indexed[hi - 1]->first;
		if (hi - lo == 1) {
			packer.push({first.front() == 'K' ? p::PageKind::Definition : p::PageKind::Namespace, 0,
						 1, first, std::string_view(first).substr(start), {}},
						indexed[lo]->second);
			drain();
			return;
		}
		auto at = start;
		while (first[at] == last[at])
			++at;
		std::vector<std::uint8_t> labels;
		for (auto i = lo; i < hi;) {
			auto j = i + 1;
			while (j < hi && indexed[j]->first[at] == indexed[i]->first[at])
				++j;
			self(self, i, j, at + 1);
			labels.push_back(static_cast<unsigned char>(indexed[i]->first[at]));
			i = j;
		}
		std::string_view key(first.data(), at);
		packer.push({p::PageKind::Branch, 0, hi - lo, key, key.substr(start), labels});
		drain();
	};
	if (!rows.empty())
		node(node, 0, indexed.size(), 0);
	packer.finish();
	drain();
	store->root.tree = packer.root();
	if (segment)
		close();
	store->root.segments = store->segments.size();
	store->index();
	return store;
}
// Applies changes through dense-pages D7: the overlay and the assembler's occurrence reuse.
static std::shared_ptr<Store> apply(const std::shared_ptr<d::StorageLedger> &account,
									const std::shared_ptr<Store> &base, const Changes &changes,
									const l::GenerationId &id, const p::Limits &limits) {
	x::TrieOverlay overlay(account, base, base->inputs, base->root, limits);
	for (const auto &[key, body] : changes) {
		overlay.change(key, body ? std::optional<std::span<const char>>(*body) : std::nullopt);
		for (unsigned turn = 0; !overlay.operation_done(); ++turn) {
			require(turn < 100000000);
			l::ValidationBudget b{8192, 16};
			overlay.step(b);
		}
	}
	overlay.seal();
	x::OverlayAssembler assembler(account, overlay, base->inputs, base->live, base->root, id,
								  base->root.cursor, limits);
	for (unsigned turn = 0; !assembler.done(); ++turn) {
		require(turn < 100000000);
		l::ValidationBudget b{8192, 16};
		assembler.step(b);
	}
	auto image = assembler.take();
	auto store = std::make_shared<Store>();
	store->root = image.root();
	store->limits = limits;
	store->segments = base->segments;
	for (const auto &s : image.segments())
		store->segments.push_back(s);
	store->index();
	return store;
}
// The delta's pages lie in base segments and in its own: not the full build's layout.
static void require_scattered(const Store &delta, const l::GenerationId &base_id) {
	std::uint64_t retained = 0, fresh = 0;
	for (std::size_t i = 0; i < delta.inputs.size(); ++i) {
		const auto &s = delta.inputs[i].ownership.segment;
		bool from_base = false;
		for (std::uint64_t ordinal = 0; ordinal < delta.inputs.size() && !from_base; ++ordinal)
			from_base = s == l::derive_segment(base_id, ordinal);
		(from_base ? retained : fresh) += delta.live[i].pages;
	}
	require(retained && fresh);
}
// A1, A2: the copy is the full build, file for file, with zero page digests.
static void check_copy(const std::shared_ptr<d::StorageLedger> &account, const Store &source,
					   const Store &full, const l::GenerationId &id, const Records &rows,
					   std::string_view label) {
	p::reset_hash_counts();
	const auto began = std::chrono::steady_clock::now();
	const auto copy = x::canonical_copy(account, source.inputs, source.root, id, source.limits);
	const auto elapsed = seconds(began);
	const auto hashes = p::hash_counts();
	require(hashes.page_calls == 0 && hashes.page_bytes == 0);
	require(hashes.pack_calls == copy.segments.size());
	require(copy.root.tree == full.root.tree && copy.root.pages == full.root.pages &&
			copy.root.bytes == full.root.bytes && copy.root.segments == full.root.segments &&
			copy.root.records == full.root.records &&
			copy.root.namespaces == full.root.namespaces);
	require(copy.segments.size() == full.segments.size());
	for (std::size_t i = 0; i < copy.segments.size(); ++i)
		require(copy.segments[i].ownership == full.segments[i].ownership &&
				copy.segments[i].bytes->view() == full.segments[i].bytes->view());
	Store copied;
	copied.root = copy.root;
	copied.limits = source.limits;
	copied.segments = copy.segments;
	copied.index();
	x::PagedReadState reader{copied.root, copied.inputs};
	for (const auto &[key, body] : rows) {
		const auto reached =
			reader.lookup(key, key.front() == 'K' ? p::PageKind::Definition : p::PageKind::Namespace);
		require(reached && std::equal(reached->begin(), reached->end(), body.begin(), body.end()));
	}
	std::cout << label << ": " << copy.root.pages << " pages, " << copy.root.bytes
			  << " page bytes, " << copy.segments.size() << " segments, " << copied.pack_bytes
			  << " pack bytes from " << source.inputs.size() << " source segments of "
			  << source.pack_bytes << " bytes; page hashes " << hashes.page_calls << " ("
			  << hashes.page_bytes << " B), pack hashes " << hashes.pack_calls << " ("
			  << hashes.pack_bytes << " B); copy " << elapsed
			  << " s; every segment equals the full build's\n";
}
static std::string key(std::string_view name, bool ns = false) {
	std::array<char, 202> bytes{};
	const auto n = ns ? p::encode_namespace_key(bytes, name) : p::encode_definition_key(bytes, name);
	return {bytes.data(), n};
}
static std::vector<char> body(std::mt19937 &rng) {
	detent::KnobDef value;
	value.default_value = static_cast<std::int64_t>(rng() % 1000);
	// Override counts from 0 to 50 give bodies up to 6.5 KB, so some leaves seal alone.
	const auto count = rng() % 3 ? rng() % 8 : rng() % 51;
	for (unsigned at = 0; at < count; ++at) {
		const auto prefix = "case" + std::to_string(at);
		value.overrides.emplace(prefix + std::string(128 - prefix.size(), 'p'),
								static_cast<std::int64_t>(rng() % 2));
	}
	std::vector<char> bytes(p::definition_size(value));
	p::encode_definition(bytes, value, 8);
	return bytes;
}
static void corpus_mode(const std::string &before_path, const std::string &after_path) {
	auto account = std::make_shared<d::StorageLedger>(16ull * 1024 * 1024 * 1024);
	const auto load_rows = [](const std::string &path) {
		Records rows;
		for (auto &row : *corpus(path))
			rows.emplace_hint(rows.end(), std::move(row.key), std::move(row.body));
		return rows;
	};
	const auto before = load_rows(before_path);
	auto after = load_rows(after_path);
	Changes changes;
	for (const auto &[k, v] : before)
		if (!after.contains(k))
			changes.emplace_back(k, std::nullopt);
	for (const auto &[k, v] : after) {
		const auto found = before.find(k);
		if (found == before.end() || found->second != v)
			changes.emplace_back(k, v);
	}
	const auto limits = limits_for(before, after);
	auto began = std::chrono::steady_clock::now();
	const auto base = build(account, before, generation('a'), limits);
	std::cout << "base: " << before.size() << " records, " << base->root.pages << " pages, "
			  << base->segments.size() << " segments; build " << seconds(began) << " s\n";
	const auto delta = apply(account, base, changes, generation('b'), limits);
	require_scattered(*delta, generation('a'));
	std::cout << "T4 churn: " << changes.size() << " changes through D7, "
			  << delta->inputs.size() << " segments hold the generation\n";
	began = std::chrono::steady_clock::now();
	const auto full = build(account, after, generation('c'), limits);
	std::cout << "full build of the target: " << seconds(began) << " s\n";
	check_copy(account, *delta, *full, generation('c'), after, "corpus canonical copy");
}
int main(int argc, char **argv) {
	report_uncaught_exceptions();
	if (argc == 4 && std::string_view(argv[1]) == "--corpus") {
		corpus_mode(argv[2], argv[3]);
		return 0;
	}
	require(argc <= 2);
	const unsigned rounds = argc == 2 ? std::stoul(argv[1]) : 6;
	require(rounds > 0 && rounds <= 10000);
	auto account = std::make_shared<d::StorageLedger>(2ull * 1024 * 1024 * 1024);
	std::mt19937 rng(0xDD2);
	Records rows{{key("ns", true), std::vector<char>(8)}};
	l::write_integer(rows.at(key("ns", true)), 0, 17, 8);
	for (unsigned i = 0; i < 3000; ++i)
		rows[key("ns:k" + std::to_string(i))] = body(rng);
	const auto limits = limits_for(rows);
	const auto base = build(account, rows, generation('a'), limits);
	require(base->segments.size() > 1);
	// The cold build is canonical: relocating it gives itself under the new id.
	check_copy(account, *base, *build(account, rows, generation('c'), limits), generation('c'),
			   rows, "cold build");
	for (unsigned trial = 0; trial < rounds; ++trial) {
		auto target = rows;
		Changes changes;
		for (unsigned j = 0; j < 1 + (trial * 97) % 600; ++j) {
			const auto k = key("ns:k" + std::to_string(rng() % 3300));
			std::optional<std::vector<char>> value;
			if (rng() % 3)
				value = body(rng);
			if (value)
				target[k] = *value;
			else
				target.erase(k);
			changes.emplace_back(k, value);
		}
		const auto delta = apply(account, base, changes, generation('b'), limits);
		require_scattered(*delta, generation('a'));
		check_copy(account, *delta, *build(account, target, generation('c'), limits),
				   generation('c'), target, "churn " + std::to_string(trial));
	}
	Records none;
	const auto empty = build(account, none, generation('a'), limits);
	const auto copy = x::canonical_copy(account, empty->inputs, empty->root, generation('c'), limits);
	require(copy.segments.empty() && copy.root.tree == p::Reference{} && !copy.root.pages);
	std::cout << "canonical copy: cold build, " << rounds
			  << " churned generations and the empty one equal their full builds\n";
}
