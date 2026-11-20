#pragma once
// The dense-distribution model's delta scenarios built in C++: a corpus, the patches
// delta_scenarios.py writes, and full builds of the base and each target.
#include "dense_corpus.hh"
#include "detent_distribution/snapshot_encoder.hh"
#include "detent_local/paged_definition.hh"
#include "paged_snapshot_builder.hh"
#include "paged_transfer.hh"
#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <set>

namespace delta_test {
using namespace dense_test;
namespace d = detent::distribution;
namespace x = detent::local::detail;

inline double seconds(std::chrono::steady_clock::time_point begin) {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
}
// SPEC.patch from delta_scenarios.py: changed or new records at their new bodies; kind 255 removes.
inline std::shared_ptr<Rows> patched(const Rows &base, const std::string &path) {
	auto data = load(path);
	require(data.size() >= 32 && std::string_view(data.data(), 8) == "DTCORP01");
	const auto count = l::read_integer(data, 8, 8), keys = l::read_integer(data, 16, 8),
			   bodies = l::read_integer(data, 24, 8);
	const auto key_at = 32 + 4 * (count + 1), body_offsets = key_at + keys,
			   body_at = body_offsets + 4 * (count + 1), kinds = body_at + bodies;
	require(kinds + count == data.size());
	auto rows = std::make_shared<Rows>();
	rows->reserve(base.size() + count);
	std::size_t i = 0;
	for (std::size_t j = 0; j < count; ++j) {
		const auto k0 = l::read_integer(data, 32 + 4 * j, 4), k1 = l::read_integer(data, 36 + 4 * j, 4),
				   b0 = l::read_integer(data, body_offsets + 4 * j, 4),
				   b1 = l::read_integer(data, body_offsets + 4 * j + 4, 4);
		require(k0 <= k1 && k1 <= keys && b0 <= b1 && b1 <= bodies);
		const std::string key(data.data() + key_at + k0, k1 - k0);
		while (i < base.size() && base[i].key < key)
			rows->push_back(base[i++]);
		const bool present = i < base.size() && base[i].key == key;
		if (static_cast<unsigned char>(data[kinds + j]) == 255) {
			require(present && b0 == b1);
			++i;
			continue;
		}
		i += present;
		rows->push_back({key, std::vector<char>(data.begin() + body_at + b0, data.begin() + body_at + b1)});
	}
	while (i < base.size())
		rows->push_back(base[i++]);
	return rows;
}

// Rows as the snapshot source a full build admits (tests/detent_local/test_dense_fullbuild.cc).
class CorpusSource final : public d::SnapshotSource {
  public:
	CorpusSource(std::shared_ptr<Rows> rows, std::shared_ptr<d::StorageLedger> account)
		: rows_(std::move(rows)), account_(std::move(account)), cursor_(empty_root().cursor) {
		cursor_.revision = 0;
		for (const auto &row : *rows_)
			if (row.key.front() == 'W') {
				const auto key = p::decode_key(row.key);
				const auto revision = l::read_integer(row.body, 0, 8);
				namespaces_.push_back({key.name_space, revision});
				cursor_.revision = std::max(cursor_.revision, revision);
			}
		for (std::size_t i = 0; i < namespaces_.size(); ++i)
			indices_.emplace(namespaces_[i].name, i);
	}
	const d::Cursor &cursor() const noexcept override { return cursor_; }
	std::shared_ptr<d::StorageLedger> storage_account() const noexcept override { return account_; }
	std::uint64_t namespace_count() const noexcept override { return namespaces_.size(); }
	d::NamespaceRevision name_space(std::uint64_t i) const override { return namespaces_.at(i); }
	void reset_records() noexcept override { ordinal_ = 0; }
	d::SnapshotRecordStep next_record() override {
		if (ordinal_ == rows_->size() || (*rows_)[ordinal_].key.front() == 'W')
			return {{}, true};
		const auto &row = (*rows_)[ordinal_++];
		const auto key = p::decode_key(row.key);
		const auto index = indices_.at(key.name_space);
		const auto def = p::validate_definition(row.body, namespaces_[index].revision);
		body_ = row.body;
		override_at_ = p::header_size(def);
		remaining_ = def.overrides;
		name_ = std::string(key.name_space) + ":" + std::string(key.local_name);
		const d::WireDefinition definition{def.default_value, {0, def.overrides}, def.rollout, def.rollout_value};
		return {d::Record{static_cast<std::uint32_t>(index), name_, def.revision, definition}, false};
	}
	std::optional<d::OverrideRow> next_override() override {
		if (!remaining_)
			return {};
		--remaining_;
		const auto value = p::read_override(body_, override_at_);
		return d::OverrideRow{value.key, value.value};
	}

  private:
	std::shared_ptr<Rows> rows_;
	std::shared_ptr<d::StorageLedger> account_;
	d::Cursor cursor_;
	std::vector<d::NamespaceRevision> namespaces_;
	std::map<std::string_view, std::size_t> indices_;
	std::string name_;
	std::span<const char> body_;
	std::size_t ordinal_ = 0, override_at_ = 0;
	std::uint32_t remaining_ = 0;
};

// A full build of `rows`: its pages by reference, and its root.
class Built {
  public:
	Built(const std::shared_ptr<Rows> &rows, char fill) {
		auto source = std::make_shared<CorpusSource>(rows, account_);
		std::vector<std::string_view> selection;
		for (std::size_t i = 0; i < source->namespace_count(); ++i)
			selection.push_back(source->name_space(i).name);
		d::Limits limits;
		limits.bytes = 4ull * 1024 * 1024 * 1024;
		limits.string_bytes = limits.bytes;
		limits.records = rows->size();
		limits.namespaces = selection.size();
		limits.storage_account = account_;
		limits.storage_bytes = account_->limit();
		d::SnapshotDecoder decoder(source->cursor(), d::NamespaceSelection(selection), limits);
		{
			d::SnapshotEncoder encoder(source, limits);
			while (!encoder.done()) {
				d::WorkBudget work{65536, 256};
				encoder.step(work);
				if (encoder.chunk_ready()) {
					const auto chunk = encoder.take_chunk();
					std::span<const char> input(chunk->data(), chunk->size());
					while (!input.empty()) {
						d::WorkBudget decode{65536, 256};
						decoder.feed(input, decode);
					}
				}
			}
		}
		auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
		limits_ = corpus_limits(snapshot->records().size(), snapshot->namespaces().size());
		l::GenerationId generation{};
		generation.fill(fill);
		x::PagedSnapshotBuilder builder(snapshot, generation, limits_);
		while (!builder.done()) {
			l::ValidationBudget budget{65536, 256};
			builder.step(budget);
		}
		image_.emplace(builder.take());
		for (const auto &segment : image_->segments())
			segments_.emplace(segment.ownership.segment, segment.bytes->view());
	}
	~Built() {
		l::ValidationBudget disposal{SIZE_MAX, SIZE_MAX};
		image_->discard(disposal);
	}
	Built(const Built &) = delete;
	const p::Reference &root() const { return image_->root().tree; }
	x::TransferTarget target() const { return {image_->root().tree.digest, image_->root().cursor.revision}; }
	std::uint64_t page_bytes() const { return image_->root().bytes; }
	const p::Limits &limits() const { return limits_; }
	const x::PagedImage &image() const { return *image_; }
	x::TransferPages pages() const {
		return [this](const p::Reference &reference) -> std::span<const char> {
			const auto bytes = segments_.at(reference.segment);
			require(reference.offset <= bytes.size() && reference.length <= bytes.size() - reference.offset);
			return {bytes.data() + reference.offset, reference.length};
		};
	}

  private:
	std::shared_ptr<d::StorageLedger> account_ = std::make_shared<d::StorageLedger>(16ull * 1024 * 1024 * 1024);
	p::Limits limits_;
	std::optional<x::PagedImage> image_;
	std::map<l::GenerationId, std::string_view> segments_;
};
// Every page of a full build in its files' order: a build writes pages in seal order.
inline std::vector<p::Reference> placement_order(const Built &built) {
	std::vector<p::Reference> out;
	for (const auto &segment : built.image().segments()) {
		const auto bytes = std::span<const char>(segment.bytes->data(), segment.bytes->size());
		for (std::size_t at = 0; at < bytes.size();) {
			const auto h = p::decode_page_header(bytes.subspan(at, l::read_integer(bytes, at + 8, 4)), built.limits());
			out.push_back({segment.ownership.segment, static_cast<std::uint32_t>(at), h.length, h.keys, h.height,
						   h.digest});
			at = (at + h.length + 7) & ~std::size_t(7);
		}
	}
	return out;
}
// Synthetic rows: `count` definitions in namespace "a" at revision 17 plus a few in "b", every
// `wide`-th one large enough to fill a page of its own.
inline std::shared_ptr<Rows> synthetic(std::size_t count, std::size_t wide = 997) {
	auto rows = std::make_shared<Rows>();
	auto definition = [](std::int64_t value, std::uint64_t revision, std::size_t overrides) {
		detent::KnobDef knob{value};
		for (std::size_t i = 0; i < overrides; ++i)
			knob.overrides.emplace("group-" + std::string(60, 'g') + std::to_string(1000 + i), std::int64_t(i));
		std::vector<char> body(p::definition_size(knob));
		p::encode_definition(body, knob, revision);
		return body;
	};
	for (std::size_t i = 0; i < count; ++i) {
		const auto name = "key" + std::to_string(100000 + i * 7);
		rows->push_back({std::string("Ka\0", 3) + name + '\0', definition(std::int64_t(i), 1 + i % 17,
																		  i % wide == 3 ? 80 : 0)});
	}
	for (std::size_t i = 0; i < 50; ++i)
		rows->push_back({std::string("Kb\0", 3) + "k" + std::to_string(i) + '\0', definition(7, 3, 0)});
	for (const auto *name : {"a", "b"}) {
		std::vector<char> watermark(8);
		l::write_integer(watermark, 0, 17, 8);
		rows->push_back({std::string("W") + name + '\0', std::move(watermark)});
	}
	std::sort(rows->begin(), rows->end(), [](const auto &a, const auto &b) { return a.key < b.key; });
	return rows;
}
// Changes `rows` as the model's scenarios do: `updates` definitions from `first`, every `stride`-th,
// get revision 18 (each namespace's watermark with them); `removed` keys go; `added` keys appear.
inline std::shared_ptr<Rows> changed(const Rows &rows, std::size_t first, std::size_t stride, std::size_t updates,
									 std::size_t removed = 0, std::size_t added = 0) {
	auto out = std::make_shared<Rows>(rows);
	std::set<std::string> touched;
	std::size_t definitions = 0, done = 0;
	std::vector<std::size_t> drop;
	for (std::size_t i = 0; i < out->size(); ++i) {
		auto &row = (*out)[i];
		if (row.key.front() != 'K' || definitions++ < first || (definitions - 1 - first) % stride)
			continue;
		if (done < updates) {
			const auto def = p::validate_definition(row.body, 17);
			l::write_integer(row.body, 0, def.revision + 18, 8);
			touched.insert(std::string(p::decode_key(row.key).name_space));
		} else if (done < updates + removed) {
			drop.push_back(i);
			touched.insert(std::string(p::decode_key(row.key).name_space));
		} else if (done < updates + removed + added) {
			auto copy = row;
			copy.key.insert(copy.key.size() - 1, "x");
			touched.insert(std::string(p::decode_key(row.key).name_space));
			out->push_back(std::move(copy)); // invalidates `row`
		}
		++done;
	}
	for (auto at = drop.rbegin(); at != drop.rend(); ++at)
		out->erase(out->begin() + *at);
	for (auto &row : *out)
		if (row.key.front() == 'W' && touched.contains(std::string(p::decode_key(row.key).name_space)))
			l::write_integer(row.body, 0, 17 + 18, 8);
	std::sort(out->begin(), out->end(), [](const auto &a, const auto &b) { return a.key < b.key; });
	return out;
}

// Every page of a tree by its entering trie path (the bytes before its top node's prefix).
inline std::map<std::string, p::Reference> pages_by_path(const Built &built) {
	std::map<std::string, p::Reference> out;
	const auto pages = built.pages();
	auto node = [&](auto &self, std::span<const char> bytes, std::size_t offset, std::string entering) -> void {
		const auto n = p::decode_node(bytes, offset, built.limits());
		if (n.kind != p::PageKind::Branch)
			return;
		const auto key = entering + std::string(n.prefix);
		for (std::size_t i = 0; i < n.labels.size(); ++i) {
			const auto path = key + n.labels[i];
			const auto slot = n.slot(i);
			if (!(slot & 0x8000)) {
				self(self, bytes, slot, path);
				continue;
			}
			const auto child = p::decode_external(bytes, slot & 0x7fff, built.limits());
			require(out.emplace(path, child).second);
			self(self, pages(child), p::page_header_bytes, path);
		}
	};
	if (built.root() != p::Reference{}) {
		out.emplace(std::string(), built.root());
		node(node, pages(built.root()), p::page_header_bytes, {});
	}
	return out;
}
// dense-pages D7's occurrence writes, the model's oracle (occurrence_diff): every target page with
// no base page of equal summary at its path, in the target's seal order.
inline std::vector<p::Reference> occurrence_writes(const Built &base, const Built &target) {
	const auto old = pages_by_path(base);
	std::set<std::pair<l::GenerationId, std::uint32_t>> written;
	for (const auto &[path, page] : pages_by_path(target)) {
		const auto found = old.find(path);
		if (found == old.end() || found->second.digest != page.digest || found->second.length != page.length ||
			found->second.keys != page.keys || found->second.height != page.height)
			written.emplace(page.segment, page.offset);
	}
	std::vector<p::Reference> out;
	for (const auto &page : placement_order(target))
		if (written.contains({page.segment, page.offset}))
			out.push_back(page);
	return out;
}
// D2, D3's wire form, written from the design for comparison with the model's streams: pages back
// to back; header digests zero; a child in the transfer has a zero digest and its derived place.
inline std::vector<char> wire_form(std::span<const p::Reference> chosen, const x::TransferPages &pages,
								   const p::Limits &limits) {
	std::map<std::pair<l::GenerationId, std::uint32_t>, std::pair<std::uint32_t, std::uint32_t>> place;
	std::uint32_t number = 0;
	std::size_t used = p::segment_bytes;
	for (const auto &page : chosen) {
		auto start = (used + 7) & ~std::size_t(7);
		if (start + page.length > p::segment_bytes)
			++number, start = 0;
		place[{page.segment, page.offset}] = {number, static_cast<std::uint32_t>(start)};
		used = start + page.length;
	}
	std::vector<char> out;
	for (const auto &page : chosen) {
		const auto bytes = pages(page);
		std::vector<char> wire(bytes.begin(), bytes.end());
		const auto externals = p::decode_page_header(bytes, limits).externals;
		const auto content = wire.size() - p::child_bytes * externals,
				   placement = wire.size() - p::placement_bytes * externals;
		for (std::uint16_t j = 0; j < externals; ++j) {
			const auto child = p::decode_external(bytes, j, limits);
			std::fill_n(wire.begin() + placement + p::placement_bytes * j, p::placement_bytes, 0);
			if (const auto found = place.find({child.segment, child.offset}); found != place.end()) {
				l::write_integer(wire, placement + p::placement_bytes * j + 12, found->second.first, 4);
				l::write_integer(wire, placement + p::placement_bytes * j + 16, found->second.second, 4);
				std::fill_n(wire.begin() + content + p::content_bytes * j, 32, 0);
			}
		}
		std::fill_n(wire.begin() + 32, 32, 0);
		out.insert(out.end(), wire.begin(), wire.end());
	}
	return out;
}
} // namespace delta_test
