#include "dense_fixture.hh"
#include "detent_local/paged_definition.hh"
#include "paged_read_state.hh"
#include "paged_navigation.hh"
#include "paged_overlay_assembler.hh"
#include "paged_packer.hh"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <random>
#include <set>
using namespace dense_test;
namespace x = l::detail;
namespace d = detent::distribution;
using Rows = std::map<std::string, std::vector<char>>;
// Orders segments as find_segment searches them: unsigned bytes, not signed chars.
static bool segment_less(const x::SegmentInput &a, const x::SegmentInput &b) {
	return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(),
					   a.ownership.segment.size()) < 0;
}
static std::size_t segment_index(std::span<const x::SegmentInput> inputs,
								 const l::StorageIdentity &id) {
	const auto i = x::find_segment(inputs, id);
	require(i != inputs.size());
	return i;
}
static std::vector<char> encoded(const detent::KnobDef &value) {
	std::vector<char> bytes(p::definition_size(value));
	p::encode_definition(bytes, value, 8);
	p::validate_definition(bytes, 17, 256);
	return bytes;
}
struct Base {
	struct Pack {
		l::SegmentOwnership ownership;
		std::vector<char> bytes;
	};
	std::vector<Pack> packs;
	std::vector<x::SegmentInput> inputs;
	std::vector<x::SegmentReachability> live;
	p::Root root = empty_root();
	std::map<std::string, p::Reference> occurrences;
	std::vector<std::string> seal_order;
	void prepare() {
		inputs.clear();
		live.clear();
		for (auto &pack : packs) {
			pack.ownership.length = pack.bytes.size();
			inputs.push_back({pack.ownership, pack.bytes});
			live.push_back({});
		}
		std::sort(inputs.begin(), inputs.end(), segment_less);
		for (const auto &[path, r] : occurrences) {
			const auto i = segment_index(inputs, r.segment);
			++live[i].pages;
			live[i].bytes += r.length;
		}
	}
};
struct Full {
	std::shared_ptr<d::StorageLedger> account;
	std::optional<x::PagedPacker> packer;
	Base result;
	std::size_t pack_limit;
	Full(std::shared_ptr<d::StorageLedger> a, std::size_t limit = p::segment_bytes)
		: account(a), pack_limit(limit) {}
	void drain() {
		for (unsigned turn = 0; !packer->accepted() && !packer->done(); ++turn) {
			require(turn < 1000000);
			l::ValidationBudget budget;
			packer->step(budget);
			if (packer->ready()) {
				const auto page = packer->page();
				auto r = page.summary;
				if (result.packs.empty() ||
					((result.packs.back().bytes.size() + 7) & ~std::size_t(7)) + r.length >
						pack_limit) {
					Base::Pack pack;
					pack.ownership.storage.fill('s');
					pack.ownership.segment.fill('b');
					l::write_integer(pack.ownership.segment, 8, result.packs.size() + 1, 8);
					result.packs.push_back(std::move(pack));
				}
				auto &pack = result.packs.back();
				r.segment = pack.ownership.segment;
				r.offset = (pack.bytes.size() + 7) & ~std::size_t(7);
				pack.bytes.resize(r.offset);
				pack.bytes.insert(pack.bytes.end(), page.bytes->begin(), page.bytes->end());
				p::verify_page({page.bytes->data(), page.bytes->size()}, r);
				result.occurrences.emplace(page.path, r);
				result.seal_order.emplace_back(page.path);
				++result.root.pages;
				result.root.bytes += r.length;
				l::ValidationBudget commit;
				require(packer->commit(r, commit));
			}
		}
	}
	void node(const std::vector<Rows::const_iterator> &rows, std::size_t lo, std::size_t hi,
			  std::size_t start) {
		const auto &first = rows[lo]->first;
		const auto &last = rows[hi - 1]->first;
		if (hi - lo == 1) {
			packer->push({first.front() == 'K' ? p::PageKind::Definition : p::PageKind::Namespace,
						 0,
						 1,
						 first,
						 std::string_view(first).substr(start),
						 {}},
						rows[lo]->second);
			drain();
			return;
		}
		auto at = start;
		while (first[at] == last[at])
			++at;
		std::vector<std::uint8_t> labels;
		for (auto i = lo; i < hi;) {
			auto j = i + 1;
			while (j < hi && rows[j]->first[at] == rows[i]->first[at])
				++j;
			node(rows, i, j, at + 1);
			labels.push_back(static_cast<unsigned char>(rows[i]->first[at]));
			i = j;
		}
		std::string_view key(first.data(), at);
		packer->push({p::PageKind::Branch, 0, hi - lo, key, key.substr(start), labels});
		drain();
	}
	Base build(const Rows &rows) {
		std::vector<Rows::const_iterator> indexed;
		for (auto i = rows.begin(); i != rows.end(); ++i) {
			indexed.push_back(i);
			if (i->first.front() == 'K')
				++result.root.records;
			else
				++result.root.namespaces;
		}
		packer.emplace(account, corpus_limits(result.root.records, result.root.namespaces));
		if (!rows.empty())
			node(indexed, 0, indexed.size(), 0);
		packer->finish();
		drain();
		result.root.tree = packer->root();
		result.root.segments = result.packs.size();
		result.prepare();
		return std::move(result);
	}
};
static std::string key(std::string_view name, bool ns = false) {
	std::array<char, 202> bytes{};
	auto n = ns ? p::encode_namespace_key(bytes, name) : p::encode_definition_key(bytes, name);
	return {bytes.data(), n};
}
static void change(x::TrieOverlay &overlay, const std::string &key,
				   const std::optional<std::vector<char>> &body, std::size_t quantum = 8192) {
	overlay.change(key, body ? std::optional<std::span<const char>>(*body) : std::nullopt);
	for (unsigned turn = 0; !overlay.operation_done(); ++turn) {
		require(turn < 1000000);
		l::ValidationBudget b{quantum, 16};
		overlay.step(b);
	}
}
static std::map<std::string, p::Reference> occurrences(std::span<const x::SegmentInput> inputs,
													   const p::Root &root) {
	std::map<std::string, p::Reference> result;
	const auto walk = [&](auto &&self, p::Reference r, std::size_t offset,
						  std::string path) -> void {
		const auto bytes = x::checked_page_bytes(inputs[segment_index(inputs, r.segment)], r);
		if (offset == p::page_header_bytes) {
			require(result.emplace(path, r).second);
			p::verify_page(bytes, r);
		}
		const auto n = p::decode_node(bytes, offset);
		path += n.prefix;
		for (std::size_t i = 0; i < n.labels.size(); ++i) {
			auto child = path + char(n.labels[i]);
			const auto slot = n.slot(i);
			if (slot & 0x8000)
				self(self, p::decode_external(bytes, slot & 0x7fff), p::page_header_bytes, child);
			else
				self(self, r, slot, child);
		}
	};
	if (root.pages)
		walk(walk, root.tree, p::page_header_bytes, {});
	return result;
}
static std::uint64_t
check(const std::shared_ptr<d::StorageLedger> &account, const std::shared_ptr<Base> &base,
	  const Rows &rows,
	  const std::vector<std::pair<std::string, std::optional<std::vector<char>>>> &changes,
	  bool relocate = false, std::size_t quantum = 8192, std::string_view label = {},
	  const std::set<std::string> *changed_paths = nullptr) {
	std::uint64_t records = 0;
	for (const auto &[key, body] : rows)
		records += key.front() == 'K';
	const auto limits = corpus_limits(std::max<std::uint64_t>(records, base->root.records),
									  std::max<std::uint64_t>(rows.size() - records, base->root.namespaces));
	x::TrieOverlay overlay(account, base, base->inputs, base->root, limits);
	if (relocate) {
		const auto id = base->inputs.front().ownership.segment;
		overlay.begin_relocation({&id, 1}, 100000);
		while (!overlay.operation_done()) {
			l::ValidationBudget b{quantum, 16};
			overlay.step(b);
		}
	} else
		for (const auto &[key, body] : changes)
			change(overlay, key, body, quantum);
	overlay.seal();
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('n');
	x::OverlayAssembler assembler(account, overlay, base->inputs, base->live, base->root, storage,
								  generation, base->root.cursor, limits);
	p::reset_hash_counts();
	for (unsigned turn = 0; !assembler.done(); ++turn) {
		require(turn < 10000000);
		l::ValidationBudget b{quantum, 16};
		assembler.step(b);
	}
	const auto hashes = p::hash_counts();
	if (relocate)
		require(hashes.page_calls == 0);
	auto image = assembler.take();
	auto merged = base->inputs;
	std::uint64_t written = 0;
	for (const auto &pack : image.segments()) {
		merged.push_back({pack.ownership, {pack.bytes->data(), pack.bytes->size()}});
		written += pack.bytes->size();
	}
	std::sort(merged.begin(), merged.end(), segment_less);
	const auto actual = occurrences(merged, image.root());
	x::PagedReadState reader{image.root(), merged};
	for (const auto &[key, body] : rows) {
		const auto reached = reader.lookup(key, key.front() == 'K' ? p::PageKind::Definition : p::PageKind::Namespace);
		require(reached && reached->size() == body.size() &&
		        std::equal(reached->begin(), reached->end(), body.begin(), body.end()));
	}
	for (const auto &[key, body] : changes)
		if (!body && !rows.contains(key))
			require(!reader.lookup(key, key.front() == 'K' ? p::PageKind::Definition : p::PageKind::Namespace));
	const auto expected = Full(account).build(rows);
	require(p::state_commitment(image.root()) == p::state_commitment(expected.root));
	require(image.root().pages == expected.root.pages && image.root().bytes == expected.root.bytes);
	require(actual.size() == expected.occurrences.size());
	std::set<std::pair<l::StorageIdentity, std::uint32_t>> extents;
	std::set<std::string> fresh_paths;
	std::uint64_t fresh_bytes = 0, offpath_pages = 0, offpath_bytes = 0;
	for (const auto &[path, r] : actual) {
		const auto &want = expected.occurrences.at(path);
		require(r.digest == want.digest && r.length == want.length && r.keys == want.keys &&
				r.height == want.height);
		require(extents.emplace(r.segment, r.offset).second);
		const auto old = base->occurrences.find(path);
		if (!relocate && old != base->occurrences.end() && old->second.digest == r.digest &&
			old->second.length == r.length && old->second.keys == r.keys &&
			old->second.height == r.height)
			require(r == old->second);
		else if (!relocate) {
			require(x::find_segment(base->inputs, r.segment) == base->inputs.size());
			fresh_paths.insert(path);
			fresh_bytes += r.length;
			if (changed_paths) {
				const auto changed = changed_paths->lower_bound(path);
				if (changed == changed_paths->end() || !changed->starts_with(path)) {
					++offpath_pages;
					offpath_bytes += r.length;
				}
			}
		}
	}
	if (!relocate) {
		std::uint64_t packed = 0, tail = 0, packs = 0;
		for (const auto &path : expected.seal_order) {
			if (!fresh_paths.contains(path))
				continue;
			const auto length = expected.occurrences.at(path).length;
			const auto offset = (tail + 7) & ~std::uint64_t(7);
			if (!packs || offset + length > p::segment_bytes) {
				packed += tail;
				tail = length;
				++packs;
			} else
				tail = offset + length;
		}
		require(written == packed + tail && image.segments().size() == packs);
	}
	std::uint64_t removed_pages = 0, removed_bytes = 0;
	for (const auto &n : assembler.removed()) {
		removed_pages += n.pages;
		removed_bytes += n.bytes;
	}
	std::uint64_t retained_pages = 0, retained_bytes = 0;
	for (const auto &[path, r] : actual)
		if (x::find_segment(base->inputs, r.segment) != base->inputs.size()) {
			++retained_pages;
			retained_bytes += r.length;
		}
	require(removed_pages + retained_pages == base->root.pages &&
			removed_bytes + retained_bytes == base->root.bytes);
	if (!label.empty()) {
		std::cout << label << ": page hashes " << hashes.page_calls << ", hash bytes "
				  << hashes.page_bytes << ", written pack bytes " << written << "\n";
		std::cout << "actual: pages=" << image.root().pages << " pageB=" << image.root().bytes
				  << " newPages=" << fresh_paths.size() << " newPageB=" << fresh_bytes
				  << " newPacks=" << image.segments().size() << " newPackB=" << written
				  << " offPages=" << offpath_pages << " offB=" << offpath_bytes << " root=";
		constexpr char digits[] = "0123456789abcdef";
		for (const auto byte : p::state_commitment(image.root())) {
			const auto value = static_cast<unsigned char>(byte);
			std::cout << digits[value >> 4] << digits[value & 15];
		}
		std::cout << "; all " << rows.size() << " bodies and canonical occurrences match full rebuild\n";
	}
	return written;
}

// Parses a DTCORP01 corpus; every key must satisfy the store's key grammar, which the
// reader enforces on each leaf it reaches.
static Rows parse_corpus(std::span<const char> file) {
	std::size_t at = 0;
	const auto read = [&](std::uint64_t size) {
		require(size <= file.size() - at);
		const auto bytes = file.subspan(at, size);
		at += size;
		return bytes;
	};
	const auto header = read(32);
	require(std::string_view(header.data(), 8) == "DTCORP01");
	const auto count = l::read_integer(header, 8, 8), key_bytes = l::read_integer(header, 16, 8),
			   body_bytes = l::read_integer(header, 24, 8);
	require(count <= 5000004 && key_bytes <= 1024ULL * 1024 * 1024 &&
			body_bytes <= 1024ULL * 1024 * 1024);
	const auto key_offsets = read(4 * (count + 1)), keys = read(key_bytes),
			   body_offsets = read(4 * (count + 1)), bodies = read(body_bytes), kinds = read(count);
	require(at == file.size());
	Rows rows;
	for (std::size_t i = 0; i < count; ++i) {
		const auto k0 = l::read_integer(key_offsets, 4 * i, 4),
				   k1 = l::read_integer(key_offsets, 4 * i + 4, 4),
				   b0 = l::read_integer(body_offsets, 4 * i, 4),
				   b1 = l::read_integer(body_offsets, 4 * i + 4, 4);
		require(k0 < k1 && k1 <= keys.size() && b0 <= b1 && b1 <= bodies.size());
		std::string key(keys.data() + k0, k1 - k0);
		try {
			require(static_cast<std::uint8_t>(p::decode_key(key).kind) ==
					static_cast<std::uint8_t>(kinds[i]));
		} catch (const std::invalid_argument &) {
			throw std::runtime_error("corpus record " + std::to_string(i) +
									 " has a key outside the store key grammar");
		}
		require(rows.empty() || rows.rbegin()->first < key);
		rows.emplace_hint(rows.end(), std::move(key),
						  std::vector<char>(bodies.begin() + b0, bodies.begin() + b1));
	}
	return rows;
}
static Rows read_corpus(const std::string &path) {
	std::ifstream file(path, std::ios::binary);
	require(bool(file));
	const std::vector<char> bytes{std::istreambuf_iterator<char>(file), {}};
	require(!file.bad());
	return parse_corpus(bytes);
}
// Encodes rows as DTCORP01, the model's corpus format.
static std::vector<char> corpus_bytes(const std::vector<std::pair<std::string, std::string>> &rows) {
	std::vector<char> keys, bodies, key_offsets(4 * (rows.size() + 1)),
		body_offsets(4 * (rows.size() + 1)), kinds;
	for (std::size_t i = 0; i < rows.size(); ++i) {
		keys.insert(keys.end(), rows[i].first.begin(), rows[i].first.end());
		bodies.insert(bodies.end(), rows[i].second.begin(), rows[i].second.end());
		l::write_integer(key_offsets, 4 * i + 4, keys.size(), 4);
		l::write_integer(body_offsets, 4 * i + 4, bodies.size(), 4);
		kinds.push_back(static_cast<char>(rows[i].first.front() == 'K' ? 2 : 3));
	}
	std::vector<char> out(32);
	std::copy_n("DTCORP01", 8, out.begin());
	l::write_integer(out, 8, rows.size(), 8);
	l::write_integer(out, 16, keys.size(), 8);
	l::write_integer(out, 24, bodies.size(), 8);
	for (const auto *part : {&key_offsets, &keys, &body_offsets, &bodies, &kinds})
		out.insert(out.end(), part->begin(), part->end());
	return out;
}
// The model's synthetic corpus holds keys such as "Kwide\0a!\0" that no store can hold: the
// packer seals them, the reader refuses the leaf, and the corpus parser rejects the file.
static void check_key_grammar(const std::shared_ptr<d::StorageLedger> &account) {
	const std::string ns("Wwide\0", 6), valid("Kwide\0a\0", 8), invalid("Kwide\0a!\0", 9);
	const auto parsed = parse_corpus(corpus_bytes({{valid, "body"}, {ns, std::string(8, '\0')}}));
	require(parsed.size() == 2 && parsed.at(valid) == std::vector<char>{'b', 'o', 'd', 'y'});
	bool refused = false;
	try {
		parse_corpus(corpus_bytes({{valid, "body"}, {invalid, "body"}, {ns, std::string(8, '\0')}}));
	} catch (const std::runtime_error &error) {
		refused = std::string_view(error.what()).ends_with("outside the store key grammar");
	}
	require(refused);
	Rows stored{{valid, {'b'}}, {invalid, {'c'}}, {ns, std::vector<char>(8)}};
	const auto base = Full(account).build(stored);
	x::PagedReadState reader{base.root, base.inputs};
	require(reader.lookup(valid, p::PageKind::Definition).has_value());
	refused = false;
	try {
		(void)reader.lookup(invalid, p::PageKind::Definition);
	} catch (const x::ReadSourceFailure &) {
		refused = true;
	}
	require(refused);
}

static void compare_corpus(const std::string &before_path, const std::string &after_path) {
	auto account = std::make_shared<d::StorageLedger>(4ULL * 1024 * 1024 * 1024);
	auto before = read_corpus(before_path);
	auto base = std::make_shared<Base>(Full(account).build(before));
	std::uint64_t pack_bytes = 0;
	for (const auto &pack : base->packs)
		pack_bytes += pack.bytes.size();
	std::cout << "base: records=" << before.size() << " pages=" << base->root.pages
			  << " pageB=" << base->root.bytes << " packs=" << base->packs.size()
			  << " packB=" << pack_bytes << "\n" << std::flush;
	auto after = read_corpus(after_path);
	std::vector<std::pair<std::string, std::optional<std::vector<char>>>> changes;
	std::set<std::string> changed_paths;
	for (auto i = before.begin(); i != before.end(); ++i) {
		const auto found = after.find(i->first);
		if (found == after.end()) {
			changes.emplace_back(i->first, std::nullopt);
			if (i != before.begin())
				changed_paths.insert(std::prev(i)->first);
			if (std::next(i) != before.end())
				changed_paths.insert(std::next(i)->first);
		}
	}
	for (const auto &[key, body] : after) {
		const auto found = before.find(key);
		if (found == before.end() || found->second != body) {
			changes.emplace_back(key, body);
			changed_paths.insert(key);
		}
	}
	before.clear();
	std::cout << "corpus changes=" << changes.size()
			  << "; opaque encoded bodies; ledger limit=4294967296 B\n" << std::flush;
	check(account, base, after, changes, false, 8192, "corpus", &changed_paths);
}

int main(int argc, char **argv) {
	report_uncaught_exceptions();
	if (argc == 4 && std::string_view(argv[1]) == "--corpus") {
		compare_corpus(argv[2], argv[3]);
		return 0;
	}
	require(argc <= 2);
	const unsigned rounds = argc == 2 ? std::stoul(argv[1]) : 50;
	require(rounds > 0 && rounds <= 1000000);
	auto account = std::make_shared<d::StorageLedger>(256 * 1024 * 1024);
	check_key_grammar(account);
	// Over 128 single-page packs: from ordinal 128 the last id byte is above 0x7f, which a
	// signed-char sort orders apart from find_segment's unsigned search.
	{
		Rows many_rows{{key("ns", true), std::vector<char>(8)}};
		for (unsigned i = 0; i < 600; ++i)
			many_rows[key("ns:w" + std::to_string(i))] = std::vector<char>(1000, char('a' + i % 26));
		auto many = std::make_shared<Base>(Full(account, 1).build(many_rows));
		require(many->packs.size() > 128);
		auto target = many_rows;
		target[key("ns:w7")] = std::vector<char>(900, 'z');
		target.erase(key("ns:w300"));
		check(account, many, target,
			  {{key("ns:w7"), target.at(key("ns:w7"))}, {key("ns:w300"), std::nullopt}});
		check(account, many, many_rows, {}, true);
	}
	Rows rows{{key("ns", true), std::vector<char>(8)}};
	l::write_integer(rows.at(key("ns", true)), 0, 17, 8);
	for (unsigned i = 0; i < 2000; ++i)
		rows[key("ns:k" + std::to_string(i))] = encoded(detent::IntDef{static_cast<std::int64_t>(i)});
	auto base = std::make_shared<Base>(Full(account).build(rows));
	require(check(account, base, rows, {}) == 0);
	auto one_update=rows;const auto updated=key("ns:k1");one_update[updated]=encoded(detent::IntDef{-1});check(account,base,one_update,{{updated,one_update.at(updated)}},false,8192,"one-update/2k");
	std::mt19937 rng(1729);
	for (unsigned trial = 0; trial < rounds; ++trial) {
		auto target = rows;
		std::vector<std::pair<std::string, std::optional<std::vector<char>>>> changes;
		for (unsigned j = 0; j < 1 + trial % 500; ++j) {
			auto k = key("ns:k" + std::to_string(rng() % 2100));
			std::optional<std::vector<char>> body;
			if (rng() % 3) {
				if (rng() % 2) body = encoded(detent::IntDef{static_cast<std::int64_t>(rng())});
				else {
					detent::BoolDef value;
					value.default_value = rng() % 2;
					const auto count = rng() % 51;
					for (unsigned at = 0; at < count; ++at) {
						const auto prefix = "case" + std::to_string(at);
						value.overrides.emplace(prefix + std::string(128 - prefix.size(), 'p'), bool(rng() % 2));
					}
					body = encoded(value);
				}
			}
			if (body)
				target[k] = *body;
			else
				target.erase(k);
			changes.emplace_back(k, body);
		}
		check(account, base, target, changes);
	}
	check(account, base, rows, {}, true);
	auto split = std::make_shared<Base>(Full(account, 4096).build(rows));
	require(split->packs.size() > 1);
	check(account, split, rows, {}, true, 1);
	const auto k = key("ns:k1");
	require(check(account, base, rows, {{k, std::vector<char>(1000)}, {k, rows.at(k)}}) == 0);
	const auto added = key("ns:new");
	require(check(account, base, rows, {{added, std::vector<char>(6000)}, {added, std::nullopt}}) ==
			0);
	Rows boundary{{key("ns", true), std::vector<char>(8)}, {key("ns:x0"), std::vector<char>(1800)},
				  {key("ns:x1"), std::vector<char>(1800)}, {key("ns:y0"), std::vector<char>(1800)},
				  {key("ns:y1"), std::vector<char>(1800)}, {key("ns:z"), std::vector<char>(100)}};
	const auto original_boundary = boundary;
	auto before = std::make_shared<Base>(Full(account).build(boundary));
	boundary[key("ns:z")] = std::vector<char>(500);
	check(account, before, boundary, {{key("ns:z"), boundary.at(key("ns:z"))}}, false, 1);
	auto after = std::make_shared<Base>(Full(account).build(boundary));
	check(account, after, original_boundary, {{key("ns:z"), original_boundary.at(key("ns:z"))}},
		  false, 1);
	Rows repeated{{key("ns", true), std::vector<char>(8)},
				  {key("ns:x"), std::vector<char>(4100)},
				  {key("ns:y"), std::vector<char>(4100)}};
	auto repeat = std::make_shared<Base>(Full(account).build(repeated));
	require(check(account, repeat, repeated, {}) == 0);
	check(account, repeat, repeated, {}, true, 1);
	Rows empty;
	auto none = std::make_shared<Base>(Full(account).build(empty));
	Rows single{{key("ns", true), std::vector<char>(8)}};
	check(account, none, single, {{key("ns", true), std::vector<char>(8)}}, false, 1);
	auto one = std::make_shared<Base>(Full(account).build(single));
	check(account, one, empty, {{key("ns", true), std::nullopt}}, false, 1);
	std::cout << "dense delta: " << rounds << " seeded mixed trials (1..500 changes), occurrence identity, no rewrite, offpath "
				 "seal, relocation zero page hashes, empty transitions passed\n";
}
