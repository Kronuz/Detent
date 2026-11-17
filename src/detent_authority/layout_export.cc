#include "layout_export.hh"
#include <cstring>
#include <set>

namespace detent::authority {
namespace r = relay;
namespace {
// Every file of `n`'s layout: the base when it has one, the blocks, then the level-0 sections. A
// tier stored inside a seal file is `seal-<index>/<its name>`.
template <class F> void for_each_file(const history::TableNamespace &n, F &&f) {
	auto name = [&](const history::TableBlock &b) {
		auto own = b.first ? history::block_name(n.name, n.incarnation, b.first, b.last)
						   : history::base_name(n.name, n.incarnation, b.last);
		return b.seal ? history::seal_name(b.seal) + "/" + own : own;
	};
	if (n.base.bytes)
		f(r::MirrorFile::Kind::Base, name(n.base), n.base);
	for (const auto &b : n.blocks)
		f(r::MirrorFile::Kind::Block, name(b), b);
	for (const auto &b : n.level0)
		f(r::MirrorFile::Kind::Section, name(b), b);
}
} // namespace

std::optional<r::MirrorTable> LayoutExport::layouts(std::span<const std::string> names, const State &sealed,
													const history::Table &table, history::Files &files) {
	if (!sealed.positions_enabled() || !table.index || table.index != sealed.applied_index())
		return std::nullopt;
	if (pruned_at_ != table.index) {
		std::set<std::string, std::less<>> reached;
		for (const auto &n : table.namespaces)
			for_each_file(n, [&](r::MirrorFile::Kind, std::string file, const history::TableBlock &) {
				reached.insert(std::move(file));
			});
		std::erase_if(sizes_, [&](const auto &entry) { return !reached.contains(entry.first); });
		pruned_at_ = table.index;
	}
	const auto policy = encode_policy(table.policy);
	r::MirrorTable out{0, table.index, {}};
	for (const auto &name : std::set<std::string>(names.begin(), names.end())) {
		const auto *entry = sealed.namespace_entry(name);
		if (!entry || !entry->live)
			continue;
		r::NamespaceLayout layout{name, entry->incarnation, table.index, table.ordinal, entry->origin, policy, {}};
		if (const auto *n = table.find(name); n && n->incarnation == entry->incarnation) {
			layout.cut = n->base.last;
			for_each_file(*n, [&](r::MirrorFile::Kind kind, std::string file, const history::TableBlock &b) {
				auto size = sizes_.find(file);
				if (size == sizes_.end())
					size = sizes_.emplace(file, history::tier_bytes(*n, b, files, sealed.limits()).size()).first;
				r::MirrorFile mirrored{kind, std::move(file), b.first, b.last, size->second, {}};
				std::memcpy(mirrored.digest.data(), b.digest.data(), mirrored.digest.size());
				layout.files.push_back(std::move(mirrored));
			});
		}
		r::check_layout(layout);
		out.entries.push_back(std::move(layout));
	}
	return out;
}

std::optional<std::string> LayoutExport::file(std::string_view name_space, std::string_view name,
											  const history::Table &table, history::Files &files, const Limits &limits,
											  std::uint64_t maximum) const {
	const auto *n = table.find(name_space);
	if (!n)
		return std::nullopt;
	const history::TableBlock *found = nullptr;
	for_each_file(*n, [&](r::MirrorFile::Kind, const std::string &file, const history::TableBlock &b) {
		if (file == name)
			found = &b;
	});
	if (!found)
		return std::nullopt;
	if (const auto size = sizes_.find(name); size != sizes_.end() && size->second > maximum)
		throw std::length_error("history file above the reply bound");
	auto bytes = history::tier_bytes(*n, *found, files, limits);
	if (bytes.size() > maximum)
		throw std::length_error("history file above the reply bound");
	return bytes;
}
} // namespace detent::authority
