#include "history_layout.hh"
#include <algorithm>
#include <stdexcept>

namespace detent::authority::history {
namespace {
using Wide = unsigned __int128;
void require(bool condition, const char *message) {
	if (!condition)
		throw std::invalid_argument(message);
}
std::uint64_t sum(const std::vector<TableBlock> &blocks, std::size_t from = 0) {
	std::uint64_t total = 0;
	for (auto i = from; i < blocks.size(); ++i)
		total += blocks[i].bytes;
	return total;
}
TableBlock describe(const Block &block, std::uint64_t seal, bool carried, std::string *encoded = nullptr) {
	auto bytes = block.first ? encode_block(block) : encode_base(block);
	TableBlock out{block.first, block.last, history::bytes(block.changes), sha256(bytes), seal, carried};
	if (encoded)
		*encoded = std::move(bytes);
	return out;
}
// The section [first, last] of `name` inside a decoded seal file.
const std::string *section_in(const SealFile &seal, std::string_view name, Revision first, Revision last,
							  const Limits &limits) {
	for (const auto &n : seal.namespaces)
		if (n.name == name && n.section) {
			auto b = decode_block(*n.section, limits);
			if (b.first == first && b.last == last)
				return &*n.section;
		}
	for (const auto &c : seal.carried) {
		auto b = decode_block(c, limits);
		if (b.name_space == name && b.first == first && b.last == last)
			return &c;
	}
	return nullptr;
}
} // namespace

std::uint64_t block_limit(const LayoutPolicy &p, std::uint64_t live) noexcept {
	return static_cast<std::uint64_t>(Wide{live} * p.alpha_num / (Wide{4} * p.alpha_den));
}

Changes read_tier(const TableNamespace &n, const TableBlock &block, Files &files, const Limits &limits,
				  const SealFile *current) {
	if (!block.first && !block.bytes)
		return {}; // An empty base has no file.
	std::string bytes;
	if (block.seal) {
		SealFile holder;
		const SealFile *seal = current && current->index == block.seal ? current : nullptr;
		if (!seal) {
			holder = decode_seal(files.read(seal_name(block.seal)), limits);
			seal = &holder;
		}
		auto found = section_in(*seal, n.name, block.first, block.last, limits);
		require(found, "a layout section is missing from its seal file");
		bytes = *found;
	} else
		bytes = files.read(block.first ? block_name(n.name, n.incarnation, block.first, block.last)
									   : base_name(n.name, n.incarnation, block.last));
	require(sha256(bytes) == block.digest, "history file digest mismatch");
	auto decoded = block.first ? decode_block(bytes, limits) : decode_base(bytes, limits);
	require(decoded.name_space == n.name && decoded.incarnation == n.incarnation && decoded.first == block.first &&
				decoded.last == block.last,
			"history file names another range");
	return std::move(decoded.changes);
}
Changes Layout::changes(const TableNamespace &n, const TableBlock &block, Files &files,
						const SealFile *current) const {
	return read_tier(n, block, files, limits_, current);
}

Step Layout::seal(const State &s, Files &files) {
	require(s.positions_enabled() && s.seal().index == s.applied_index(), "seal outside a sealed state");
	SealFile input;
	input.index = s.applied_index();
	input.ordinal = s.seal().ordinal;
	input.previous = table_.index;
	input.policy = s.policy();
	const auto &limits = s.limits();
	std::map<std::string, Changes> bases, sections;
	if (!input.ordinal) {
		// Seal 0, the enabling entry: every namespace's state is its first base.
		s.for_each([&](const std::string &key, const Record &r) {
			bases[key.substr(0, key.find(':'))].push_back({key, r.revision, encode_definition(*r.value, limits)});
		});
		s.for_each_directory_entry(0, [&](std::uint64_t, const std::string &name, const Namespace &entry) {
			if (entry.live)
				input.namespaces.push_back(
					{name, entry.incarnation, entry.directory, entry.origin, entry.position, entry.bytes, false, true, {}});
		});
		std::sort(input.namespaces.begin(), input.namespaces.end(),
				  [](const auto &a, const auto &b) { return a.name < b.name; });
		return apply(std::move(input), files, bases);
	}
	s.for_each_sealed_change([&](const std::string &key, Revision at) {
		auto record = s.find(key);
		require(!record || record->revision == at, "accumulator and state disagree");
		Change c{key, at, {}};
		if (record)
			c.definition = encode_definition(*record->value, limits);
		sections[key.substr(0, key.find(':'))].push_back(std::move(c));
	});
	std::vector<std::string> names;
	for (const auto &[name, _] : sections)
		names.push_back(name);
	for (const auto &n : table_.namespaces)
		names.push_back(n.name);
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	for (const auto &name : names) {
		const auto *entry = s.namespace_entry(name);
		const auto *held = table_.find(name);
		const auto changed = sections.find(name);
		if (!entry || !entry->live) {
			if (held)
				input.namespaces.push_back({name, held->incarnation, held->directory, held->origin, held->position,
											0, true, false, {}});
			continue;
		}
		if (changed == sections.end())
			continue; // Unchanged since the previous seal (a new incarnation always has changes).
		const auto same = held && held->incarnation == entry->incarnation;
		Block section{name, entry->incarnation, (same ? held->position : entry->origin) + 1, entry->position,
					  std::move(changed->second)};
		input.namespaces.push_back({name, entry->incarnation, entry->directory, entry->origin, entry->position,
									entry->bytes, false, false, encode_block(section)});
	}
	return apply(std::move(input), files, bases);
}

Step Layout::replay(std::string_view bytes, Files &files) {
	auto recorded = decode_seal(bytes, limits_);
	require(recorded.ordinal, "seal 0 is never replayed");
	auto input = recorded;
	input.carried.clear();
	input.outputs.clear();
	for (const auto &n : input.namespaces)
		require(!n.first_base, "first bases are taken only at the enabling entry");
	auto step = apply(std::move(input), files, {});
	require(step.seal == recorded, "seal file does not match its recomputation");
	return step;
}

Step Layout::apply(SealFile input, Files &files, const std::map<std::string, Changes> &bases) {
	require(empty() ? !input.ordinal : input.ordinal == table_.ordinal + 1 && input.previous == table_.index,
			"seal out of order");
	require(input.index > table_.index, "seal index does not advance");
	const auto &p = input.policy;
	Table next = table_;
	Step step;
	auto output = [&](const std::string &name_space, const Identity &incarnation, const Block &block,
					  TableBlock &meta) {
		std::string encoded;
		meta = describe(block, 0, false, &encoded);
		if (!meta.first && !meta.bytes) {
			meta.digest = {};
			return; // An empty base is not a file.
		}
		Output o{name_space, incarnation, meta.first, meta.last, meta.bytes, meta.digest, !meta.first};
		step.written.emplace_back(o.name(), std::move(encoded));
		step.seal.outputs.push_back(std::move(o));
	};
	for (const auto &n : input.namespaces) {
		auto it = std::lower_bound(next.namespaces.begin(), next.namespaces.end(), n.name,
								   [](const TableNamespace &t, const std::string &k) { return t.name < k; });
		const bool present = it != next.namespaces.end() && it->name == n.name;
		if (n.dropped) {
			require(present && it->incarnation == n.incarnation && !n.section, "drop of an absent namespace");
			next.namespaces.erase(it);
			continue;
		}
		if (!present || it->incarnation != n.incarnation) {
			TableNamespace fresh{n.name, n.incarnation, n.directory, n.origin, n.origin, 0, 0, false, {0, n.origin}};
			it = present ? (*it = std::move(fresh), it) : next.namespaces.insert(it, std::move(fresh));
		}
		require(n.position >= it->position && n.origin == it->origin, "seal namespace goes backwards");
		it->directory = n.directory;
		it->live = n.live;
		if (n.first_base) {
			require(!input.ordinal && !n.section && n.origin == n.position, "invalid first base");
			auto found = bases.find(n.name);
			Block base{n.name, n.incarnation, 0, n.position, found == bases.end() ? Changes{} : found->second};
			require(history::bytes(base.changes) == n.live, "first base and live size disagree");
			output(n.name, n.incarnation, base, it->base);
			it->position = n.position;
			continue;
		}
		require(n.section.has_value(), "a changed namespace without a section");
		auto section = decode_block(*n.section, limits_);
		require(section.name_space == n.name && section.incarnation == n.incarnation &&
					section.first == it->position + 1 && section.last == n.position,
				"section does not continue the namespace");
		it->level0.push_back(describe(section, input.index, false));
		it->position = n.position;
		it->touched = true;
	}
	for (auto &n : next.namespaces) {
		if (n.level0.empty())
			continue;
		const auto limit = block_limit(p, n.live);
		const bool periodic = (input.ordinal + n.directory) % p.period == 0;
		if (!periodic && sum(n.level0) < limit)
			continue;
		Changes merged;
		for (const auto &section : n.level0)
			merged = merge(merged, changes(n, section, files, &input));
		Block rolled{n.name, n.incarnation, n.level0.front().first, n.level0.back().last, std::move(merged)};
		const auto rolled_bytes = history::bytes(rolled.changes);
		const bool touched = n.touched;
		n.touched = false;
		n.level0.clear();
		if (periodic && rolled_bytes < std::min(p.carry_bytes, limit) && touched && n.carries < p.carry_count) {
			// A small roll-up is written back as one section of this seal file.
			++n.carries;
			n.level0.push_back(describe(rolled, input.index, true));
			input.carried.push_back(encode_block(rolled));
			continue;
		}
		n.carries = 0;
		// Squash in memory; only the blocks and base that result are written.
		struct Item {
			TableBlock meta;
			std::optional<Changes> body;
		};
		std::vector<Item> items;
		for (const auto &b : n.blocks)
			items.push_back({b, {}});
		items.push_back({describe(rolled, 0, false), std::move(rolled.changes)});
		auto body = [&](Item &item) -> Changes & {
			if (!item.body)
				item.body = changes(n, item.meta, files, &input);
			return *item.body;
		};
		while (items.size() >= 2) {
			auto &older = items[items.size() - 2], &younger = items.back();
			if (older.meta.bytes >= p.ratio * younger.meta.bytes)
				break;
			Block joined{n.name, n.incarnation, older.meta.first, younger.meta.last, merge(body(older), body(younger))};
			const auto size = history::bytes(joined.changes);
			if (size > limit && size > std::max(older.meta.bytes, younger.meta.bytes))
				break; // Past L only a merge that grows nothing proceeds.
			Item item{describe(joined, 0, false), std::move(joined.changes)};
			items.pop_back();
			items.back() = std::move(item);
		}
		// The newest block is never cut.
		std::optional<Changes> base;
		while (items.size() >= 2) {
			std::uint64_t younger = 0;
			for (std::size_t i = 1; i < items.size(); ++i)
				younger += items[i].meta.bytes;
			const Wide budget = Wide{p.alpha_num} * n.live;
			Cut rule;
			if (Wide{younger} * p.alpha_den >= budget)
				rule = Cut::Cost;
			else if (Wide{younger + items.front().meta.bytes} * p.alpha_den > budget * p.cap)
				rule = Cut::Cap;
			else if (items.size() > max_blocks)
				rule = Cut::Count;
			else
				break;
			++cuts_[n.name][static_cast<unsigned>(rule)];
			if (!base)
				base = changes(n, n.base, files, &input);
			base = merge(*base, body(items.front()), true);
			n.base.last = items.front().meta.last;
			items.erase(items.begin());
		}
		if (base)
			output(n.name, n.incarnation, Block{n.name, n.incarnation, 0, n.base.last, std::move(*base)}, n.base);
		n.blocks.clear();
		for (auto &item : items) {
			if (item.body)
				output(n.name, n.incarnation,
					   Block{n.name, n.incarnation, item.meta.first, item.meta.last, std::move(*item.body)},
					   item.meta);
			n.blocks.push_back(item.meta);
		}
	}
	next.index = input.index;
	next.ordinal = input.ordinal;
	next.policy = p;
	input.outputs = std::move(step.seal.outputs);
	step.seal = std::move(input);
	// Outputs are listed in a canonical order: by name.
	std::sort(step.seal.outputs.begin(), step.seal.outputs.end(),
			  [](const Output &a, const Output &b) { return a.name() < b.name(); });
	std::sort(step.written.begin(), step.written.end());
	step.bytes = encode(step.seal);
	if (step.seal.ordinal % p.period == 0)
		step.table = next;
	table_ = std::move(next);
	return step;
}

Answer Layout::answer(std::string_view name, const Identity &incarnation, Revision after, Revision ring_floor,
					  Files &files) const {
	Answer a;
	const auto *n = table_.find(name);
	if (n && n->incarnation == incarnation && (after >= ring_floor || after >= n->position)) {
		a.tier = Tier::Ring;
		a.through = after;
		return a;
	}
	a.through = n ? n->position : 0;
	auto read = [&](const TableBlock &b) {
		a.read += b.bytes;
		a.names.push_back(b.seal ? seal_name(b.seal) + "/" + block_name(n->name, n->incarnation, b.first, b.last)
						  : b.first ? block_name(n->name, n->incarnation, b.first, b.last)
									: base_name(n->name, n->incarnation, b.last));
		return changes(*n, b, files, nullptr);
	};
	if (!n || n->incarnation != incarnation || after < n->base.last) {
		a.tier = Tier::FullCopy;
		if (!n)
			return a;
		Changes copy = read(n->base);
		for (const auto *list : {&n->blocks, &n->level0})
			for (const auto &b : *list)
				copy = merge(copy, read(b), true);
		a.sent = bytes(copy);
		a.parts.push_back(std::move(copy));
		return a;
	}
	a.tier = Tier::Blocks;
	// The block holding after + 1 trimmed, every younger block whole, then level-0 merged.
	for (const auto &b : n->blocks)
		if (b.last > after)
			a.parts.push_back(b.first <= after ? trim(read(b), after) : read(b));
	Changes suffix;
	for (const auto &b : n->level0)
		if (b.last > after)
			suffix = merge(suffix, read(b));
	if (!suffix.empty())
		a.parts.push_back(trim(suffix, after));
	for (const auto &part : a.parts)
		a.sent += bytes(part);
	return a;
}
} // namespace detent::authority::history
