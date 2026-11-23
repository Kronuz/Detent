#include "dense_graph_fixture.hh"
#include "paged_navigation.hh"
#include "paged_node_step.hh"
#include "paged_read_state.hh"
#include "paged_trie_overlay.hh"
#include <iostream>
using namespace dense_graph_test;
namespace x = l::detail;
namespace {
// One page of hand-placed nodes over the fixture's root reference, marked verified, so only the
// walkers' own bounds stand between a forged slot and the walk.
struct Forged {
	std::shared_ptr<Fixture> f = fixture();
	std::vector<char> nodes;
	std::unique_ptr<x::VerifiedPages> verified;
	std::size_t branch() {
		const auto at = p::page_header_bytes + nodes.size();
		nodes.insert(nodes.end(), {char(p::PageKind::Branch), 0, 1, 'K', 'W', 0, 0, 0, 0});
		return at;
	}
	std::size_t leaf() {
		const auto at = p::page_header_bytes + nodes.size();
		nodes.insert(nodes.end(), {char(p::PageKind::Definition), 0, 1, 0, 0});
		return at;
	}
	void slot(std::size_t branch, std::size_t index, std::uint16_t target) {
		l::write_integer(nodes, branch - p::page_header_bytes + 5 + index * 2, target, 2);
	}
	void seal(std::uint16_t count) {
		check(f->pages.size() == 1);
		auto &reference = f->root.tree;
		reference.length = 32760;
		f->root.bytes = reference.length;
		f->bytes.assign(reference.length, 0);
		const p::PageHeader header{reference.length, 0, count, reference.keys, reference.height,
								   reference.digest};
		const auto encoded = p::encode_page_header(header, p::Limits{});
		std::copy(encoded.begin(), encoded.end(), f->bytes.begin());
		std::copy(nodes.begin(), nodes.end(), f->bytes.begin() + p::page_header_bytes);
		f->refresh();
		verified = std::make_unique<x::VerifiedPages>(f->bytes.size(), f->account);
		verified->set(reference.offset);
		f->inputs[0].pages = verified.get();
	}
	std::span<const char> page() const { return {f->bytes.data(), f->root.tree.length}; }
	void refuse(std::string_view key) {
		const auto hashes = p::hash_counts();
		x::PagedReadState state{f->root, f->inputs};
		rejects<x::ReadSourceFailure>([&] { state.lookup(key, p::PageKind::Definition); });
		rejects<std::invalid_argument>([&] {
			const auto &row = f->rows.front();
			x::TrieOverlay overlay(f->account, f, f->inputs, f->root);
			overlay.change(row.key, std::span<const char>(row.body));
			for (unsigned turns = 0; !overlay.operation_done(); ++turns) {
				check(turns < 10000);
				l::ValidationBudget budget{1024, 16};
				overlay.step(budget);
			}
		});
		check(p::hash_counts() == hashes);
	}
};
// The step itself: forward to the parent's end or past it, never back, inward or external.
void step() {
	Forged g;
	const auto root = g.branch(), a = g.branch(), b = g.leaf();
	g.slot(root, 0, a);
	g.slot(root, 1, b);
	g.slot(a, 0, b);
	g.slot(a, 1, b);
	g.seal(3);
	const auto node = p::NodeAt{}.decode(g.page(), p::Limits{});
	check(p::NodeAt{}.first() && p::NodeAt{}.offset() == p::page_header_bytes);
	check(node.kind == p::PageKind::Branch && node.end == a);
	const auto child = p::NodeAt::child(node, node.slot(0));
	check(child == p::NodeAt::after(node) && child.offset() == a && !child.first());
	const auto inner = child.decode(g.page(), p::Limits{});
	check(p::NodeAt::child(inner, inner.slot(1)).offset() == b);
	for (const std::size_t target : {root, root + 1, a - 1, std::size_t(0), std::size_t(0x8000) | a})
		rejects<std::invalid_argument>([&] { p::NodeAt::child(node, std::uint16_t(target)); });
	rejects<std::invalid_argument>([&] { p::NodeAt::child(inner, std::uint16_t(a)); });
	rejects<std::invalid_argument>(
		[&] { p::NodeAt::child(node, std::uint16_t(g.page().size() - 2)).decode(g.page(), p::Limits{}); });
}
// Walkers refuse a page whose inline slot points at or before its parent. Without the step's bound
// the key-driven lookup loops back over the root and reports the key absent.
void walkers() {
	{
		Forged g;
		const auto root = g.branch();
		g.slot(root, 0, root);
		g.slot(root, 1, root);
		g.seal(1);
		g.refuse("KK");
	}
	{
		Forged g;
		const auto root = g.branch(), a = g.branch();
		g.slot(root, 0, a);
		g.slot(root, 1, a);
		g.slot(a, 0, root);
		g.slot(a, 1, root);
		g.seal(2);
		g.refuse("KKK");
	}
	{
		Forged g;
		const auto root = g.branch(), a = g.branch();
		g.slot(root, 0, a);
		g.slot(root, 1, a);
		g.slot(a, 0, a);
		g.slot(a, 1, a);
		g.seal(2);
		g.refuse("KKK");
	}
}
// Valid pages read back every row through the same steps.
void control() {
	const auto f = fixture();
	x::PagedReadState state{f->root, f->inputs};
	for (const auto &row : f->rows) {
		const auto kind = p::decode_key(row.key).kind;
		const auto body = state.lookup(row.key, kind);
		check(body && std::equal(body->begin(), body->end(), row.body.begin(), row.body.end()));
		check(!state.lookup(row.key + "x", kind));
	}
}
} // namespace
int main() {
	step();
	walkers();
	control();
	std::cout << "dense walkers: forward inline steps, refused back edges, exact reads passed\n";
}
