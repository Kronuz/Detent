#pragma once
#include "detent_local/paged_definition.hh"
#include "paged_graph_validator.hh"
#include "paged_packer.hh"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <source_location>
#include <vector>

namespace dense_graph_test {
namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
inline void check(bool value, std::source_location at = std::source_location::current()) {
	if (!value)
		throw std::runtime_error(std::string("dense validation invariant at ") + at.file_name() + ":" +
								 std::to_string(at.line()));
}
template <class E, class F> void rejects(F call, std::source_location at = std::source_location::current()) {
	try {
		call();
	} catch (const E &) {
		return;
	}
	throw std::runtime_error("malformed dense input accepted at " + std::string(at.file_name()) + ":" + std::to_string(at.line()));
}
struct Row {
	std::string key;
	std::vector<char> body;
};
struct Page {
	p::Reference reference;
	std::vector<char> bytes;
	std::string path;
};
struct Fixture {
	std::shared_ptr<d::StorageLedger> account =
	    std::make_shared<d::StorageLedger>(64 * p::segment_bytes);
	std::vector<Row> rows;
	std::vector<Page> pages;
	std::vector<char> bytes;
	std::array<l::detail::SegmentInput, 1> inputs;
	l::StorageIdentity storage{}, segment = [] {
		l::StorageIdentity id;
		id.fill('s');
		return id;
	}();
	p::Root root;
	void refresh() {
		l::SegmentOwnership ownership;
		ownership.storage = storage;
		ownership.segment = segment;
		ownership.length = bytes.size();
		ownership.checksum = p::pack_commitment(bytes);
		inputs[0] = {ownership, bytes};
	}
};
inline std::shared_ptr<Fixture> seal(std::shared_ptr<Fixture> result, std::size_t definitions,
                                     std::size_t namespaces) {
	std::sort(result->rows.begin(), result->rows.end(),
	          [](const Row &a, const Row &b) { return a.key < b.key; });
	auto read = [](const void *input, std::uint64_t ordinal, std::span<char> out) {
		const auto &row = static_cast<const Fixture *>(input)->rows.at(ordinal);
		std::copy(row.key.begin(), row.key.end(), out.begin());
		return row.key.size();
	};
	l::detail::TrieCursor cursor(result->account, result, read, result->rows.size());
	l::detail::PagedPacker packer(result->account);
	bool finishing = false;
	for (std::size_t steps = 0; !packer.done(); ++steps) {
		check(steps < 1000000);
		l::ValidationBudget budget;
		packer.step(budget);
		if (packer.ready()) {
			const auto page = packer.page();
			auto reference = page.summary;
			reference.segment = result->segment;
			result->bytes.resize((result->bytes.size() + 7) & ~std::size_t(7));
			reference.offset = result->bytes.size();
			result->bytes.insert(result->bytes.end(), page.bytes->data(),
			                     page.bytes->data() + page.bytes->size());
			result->pages.push_back(
			    {reference,
				 std::vector<char>(page.bytes->data(), page.bytes->data() + page.bytes->size()),
                 std::string(page.path)});
			result->root.bytes += reference.length;
			check(packer.commit(reference, budget));
			continue;
		}
		if (!packer.accepted())
			continue;
		if (cursor.ready()) {
			check(cursor.commit(budget));
		}
		cursor.step(budget);
		if (cursor.ready()) {
			const auto event = cursor.event();
			packer.push(event, event.kind == p::PageKind::Branch
			                       ? std::span<const char>{}
								   : result->rows[event.ordinal].body);
		} else if (cursor.done() && !finishing) {
			packer.finish();
			finishing = true;
		}
	}
	result->root.tree = packer.root();
	result->root.pages = result->pages.size();
	result->root.segments = 1;
	result->root.records = definitions;
	result->root.namespaces = namespaces;
	result->root.cursor.cluster.fill('c');
	result->root.cursor.group.fill('g');
	result->root.cursor.restore_epoch.fill('e');
	result->root.cursor.scope.fill('s');
	result->root.cursor.revision = 10;
	result->refresh();
	return result;
}
inline std::shared_ptr<Fixture> fixture(std::size_t definitions = 32) {
	auto result = std::make_shared<Fixture>();
	result->storage.fill('x');
	std::array<char, 202> key{};
	for (std::size_t i = 0; i < definitions; ++i) {
		const auto name = "ns:k" + std::to_string(100000 + i);
		const auto length = p::encode_definition_key(key, name);
		detent::KnobDef value{static_cast<std::int64_t>(i)};
		std::vector<char> body(p::definition_size(value));
		p::encode_definition(body, value, 8);
		result->rows.push_back({std::string(key.data(), length), std::move(body)});
	}
	const auto length = p::encode_namespace_key(key, "ns");
	std::vector<char> watermark(8);
	l::write_integer(watermark, 0, 9, 8);
	result->rows.push_back({std::string(key.data(), length), std::move(watermark)});
	return seal(std::move(result), definitions, 1);
}

} // namespace dense_graph_test
