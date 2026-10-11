#include "detent_local/paged_definition.hh"
#include "paged_graph_validator.hh"
#include "paged_segment_builder.hh"
#include "paged_trie_cursor.hh"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("canonical trie stream invariant");
}
template <class Error, class F> void rejects(F call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid trie cursor operation accepted");
}
struct Source {
	std::vector<std::string> keys;
	std::shared_ptr<const d::OwnedBuffer> bytes;
	std::array<l::detail::SegmentInput, 1> segments;
};
std::size_t key(const void *owner, std::uint64_t ordinal, std::span<char> output) {
	const auto &text = static_cast<const Source *>(owner)->keys.at(ordinal);
	check(text.size() <= output.size());
	std::copy(text.begin(), text.end(), output.begin());
	return text.size();
}
std::span<const char> body(const Source &source, std::size_t ordinal, std::span<char> output) {
	if (p::decode_key(source.keys[ordinal]).kind == p::PageKind::Namespace) {
		l::write_integer(output, 0, source.keys.size() + 1, 8);
		return output.first(8);
	}
	p::encode_definition(output.first(24),
						 detent::KnobDef{detent::IntDef{static_cast<std::int64_t>(ordinal)}}, ordinal + 1);
	return output.first(24);
}
p::Digest oracle(const Source &source, std::size_t begin, std::size_t end, int parent = -1) {
	if (begin == end)
		return p::empty_commitment();
	const auto &first = source.keys[begin];
	if (end - begin == 1) {
		std::array<char, 24> value{};
		return p::logical_commitment(p::decode_key(first).kind, std::string_view(first).substr(parent + 1),
									 {}, body(source, begin, value));
	}
	const auto &last = source.keys[end - 1];
	std::size_t common = 0;
	while (common < first.size() && first[common] == last[common])
		++common;
	std::vector<p::Child> children;
	for (auto next = begin; next < end;) {
		const auto label = static_cast<unsigned char>(source.keys[next][common]);
		auto stop = next + 1;
		while (stop < end && static_cast<unsigned char>(source.keys[stop][common]) == label)
			++stop;
		p::Child child;
		child.label = label;
		child.reference.logical = oracle(source, next, stop, common);
		children.push_back(child);
		next = stop;
	}
	return p::logical_commitment(
		p::PageKind::Branch, std::string_view(first).substr(parent + 1, common - parent - 1), children, {});
}
void qualify(std::vector<std::string> keys) {
	auto source = std::make_shared<Source>();
	source->keys = std::move(keys);
	auto account = std::make_shared<d::StorageLedger>(8 * p::segment_bytes);
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	std::uint64_t pages = 0, extents = 0;
	std::uint32_t records = 0, namespaces = 0;
	{
		l::detail::TrieCursor cursor(account, source, key, source->keys.size());
		l::detail::SegmentBuilder segment(account, storage, generation, 0);
		unsigned turns = 0;
		while (!cursor.done()) {
			l::ValidationBudget budget{1, 1};
			cursor.step(budget);
			check(budget.bytes <= 1 && budget.rows <= 1 && ++turns < (source->keys.size() + 1) * 4096);
			if (!cursor.ready())
				continue;
			const auto event = cursor.event();
			std::array<char, 24> value{};
			const auto payload = event.kind == p::PageKind::Branch ? std::span<const char>{}
																   : body(*source, event.ordinal, value);
			l::detail::PageBuilder page(account, event.kind, event.keys, event.prefix, event.children,
										payload);
			while (!page.done()) {
				l::ValidationBudget work{1, 1};
				page.step(work);
			}
			check(segment.fits(page));
			segment.append(page);
			while (!segment.page_ready()) {
				l::ValidationBudget work{1, 1};
				segment.step(work);
			}
			const auto reference = segment.reference();
			++pages;
			extents += reference.length;
			if (event.kind == p::PageKind::Definition)
				++records;
			if (event.kind == p::PageKind::Namespace)
				++namespaces;
			l::ValidationBudget disposal{0, 1};
			check(page.discard(disposal));
			for (;;) {
				l::ValidationBudget work{1, 1};
				if (cursor.commit(reference, work))
					break;
				check(cursor.ready());
			}
		}
		const auto root = cursor.root();
		if (!pages)
			check(root == p::Reference{});
		else {
			check(root.logical == oracle(*source, 0, source->keys.size()));
			segment.seal();
			l::ValidationBudget budget;
			segment.step(budget);
			source->bytes = segment.bytes();
			source->segments[0] = {segment.ownership(), {source->bytes->data(), source->bytes->size()}};
			p::Root manifest;
			manifest.cursor.cluster.fill('c');
			manifest.cursor.group.fill('g');
			manifest.cursor.restore_epoch.fill('e');
			manifest.cursor.scope.fill('s');
			manifest.cursor.revision = source->keys.size() + 1;
			manifest.records = records;
			manifest.namespaces = namespaces;
			manifest.pages = pages;
			manifest.bytes = extents;
			manifest.segments = 1;
			manifest.tree = root;
			manifest.logical = root.logical;
			l::detail::GraphValidator validator(account, source, source->segments, manifest, storage);
			while (!validator.done()) {
				l::ValidationBudget work;
				validator.step(work);
			}
			check(validator.root().pages == pages);
		}
		l::ValidationBudget disposal{0, 1};
		check(cursor.discard(disposal));
	}
	source->bytes.reset();
	check(!account->used());
}
} // namespace
int main() {
	qualify({});
	qualify({std::string("Wns\0", 4)});
	qualify({std::string("Kns\0k\0", 6), std::string("Wns\0", 4)});
	qualify({std::string("Kns\0k\0", 6), std::string("Kns\0kk\0", 7), std::string("Kns\0kz\0", 7),
			 std::string("Wns\0", 4)});
	qualify({std::string("Ka\0x\0", 5), std::string("Ka.b\0x\0", 7), std::string("Wa\0", 3),
			 std::string("Wa.b\0", 5)});
	const std::string long_name(198, 'n');
	qualify({'K' + long_name + std::string("\0k\0", 3), 'W' + long_name + '\0'});
	std::vector<std::string> keys;
	for (unsigned i = 0; i < 1000; ++i) {
		auto digits = std::to_string(i);
		keys.push_back(std::string("Kns\0", 4) + std::string(4 - digits.size(), '0') + digits + '\0');
	}
	keys.push_back(std::string("Wns\0", 4));
	qualify(std::move(keys));
	for (auto bad : {std::vector<std::string>{std::string("Wns\0", 4), std::string("Kns\0k\0", 6)},
					 std::vector<std::string>{std::string("Wns\0", 4), std::string("Wns\0", 4)},
					 std::vector<std::string>{std::string("Qns\0", 4)}}) {
		auto source = std::make_shared<Source>();
		source->keys = std::move(bad);
		auto account = std::make_shared<d::StorageLedger>(p::segment_bytes);
		l::detail::TrieCursor cursor(account, source, key, source->keys.size());
		rejects<std::invalid_argument>([&] {
			while (!cursor.ready()) {
				l::ValidationBudget budget;
				cursor.step(budget);
			}
		});
		check(!cursor.done() && !cursor.ready());
		rejects<std::logic_error>([&] { cursor.root(); });
		l::ValidationBudget disposal{0, 1};
		check(cursor.discard(disposal) && !account->used());
	}
	{
		auto source = std::make_shared<Source>();
		source->keys = {std::string("Kns\0k\0", 6), std::string("Wns\0", 4)};
		auto account = std::make_shared<d::StorageLedger>(p::segment_bytes);
		std::size_t initial, peak;
		{
			l::detail::TrieCursor cursor(account, source, key, source->keys.size());
			initial = account->used();
			while (!cursor.ready()) {
				l::ValidationBudget budget;
				cursor.step(budget);
			}
			peak = account->peak();
			l::ValidationBudget empty{0, 0};
			check(!cursor.discard(empty));
			l::ValidationBudget first{0, 1};
			check(!cursor.discard(first) && !cursor.ready());
			rejects<std::logic_error>([&] { cursor.step(first); });
			for (;;) {
				l::ValidationBudget budget{0, 1};
				if (cursor.discard(budget))
					break;
			}
			check(!account->used());
		}
		auto short_initial = std::make_shared<d::StorageLedger>(initial - 1);
		rejects<d::StoragePressure>(
			[&] { l::detail::TrieCursor cursor(short_initial, source, key, source->keys.size()); });
		check(!short_initial->used());
		auto short_work = std::make_shared<d::StorageLedger>(peak - 1);
		l::detail::TrieCursor cursor(short_work, source, key, source->keys.size());
		rejects<d::StoragePressure>([&] {
			while (!cursor.ready()) {
				l::ValidationBudget budget;
				cursor.step(budget);
			}
		});
		for (;;) {
			l::ValidationBudget budget{0, 1};
			if (cursor.discard(budget))
				break;
		}
		check(!short_work->used());
	}
	std::cout << "streamed canonical trie matches independent grouping and complete graph validation\n";
}
