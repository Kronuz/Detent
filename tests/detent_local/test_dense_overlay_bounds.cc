#include "dense_graph_fixture.hh"
#include "paged_navigation.hh"
#include "paged_trie_overlay.hh"
#include <iostream>
#include <limits>
using namespace dense_graph_test;
namespace x = l::detail;
namespace {
void change(x::TrieOverlay &overlay, const Row &row) {
	overlay.change(row.key, std::span<const char>(row.body));
	for (unsigned turns = 0; !overlay.operation_done(); ++turns) {
		check(turns < 10000);
		l::ValidationBudget budget{1024, 16};
		overlay.step(budget);
	}
}
void control() {
	auto f = fixture();
	x::VerifiedPages verified(f->bytes.size(), f->account);
	f->inputs[0].pages = &verified;
	for (const auto &row : f->rows) {
		x::TrieOverlay overlay(f->account, f, f->inputs, f->root);
		change(overlay, row);
		check(!overlay.changed());
		const auto prior = overlay.previous_body();
		check(prior && std::equal(prior->begin(), prior->end(), row.body.begin(), row.body.end()));
	}
	const auto before = p::hash_counts();
	x::TrieOverlay overlay(f->account, f, f->inputs, f->root);
	change(overlay, f->rows.front());
	check(p::hash_counts() == before);
}
struct Graph {
	std::shared_ptr<Fixture> f = fixture();
	std::vector<char> nodes;
	std::size_t branch(std::string_view prefix = {}) {
		const auto at = p::page_header_bytes + nodes.size();
		nodes.push_back(char(p::PageKind::Branch));
		nodes.push_back(char(prefix.size()));
		nodes.push_back(1);
		nodes.insert(nodes.end(), prefix.begin(), prefix.end());
		nodes.insert(nodes.end(), {'K', 'W', 0, 0, 0, 0});
		return at;
	}
	std::size_t leaf() {
		const auto at = p::page_header_bytes + nodes.size();
		nodes.insert(nodes.end(), {char(p::PageKind::Definition), 0, 1, 0, 0});
		return at;
	}
	void slot(std::size_t branch, std::size_t index, std::uint16_t target) {
		const auto prefix = static_cast<unsigned char>(nodes[branch - p::page_header_bytes + 1]);
		l::write_integer(nodes, branch - p::page_header_bytes + 5 + prefix + index * 2, target, 2);
	}
	void refuse(std::uint16_t count, bool external = false) {
		check(f->pages.size() == 1);
		auto &reference = f->root.tree;
		reference.length = 32760;
		f->root.bytes = reference.length;
		f->bytes.assign(reference.length, 0);
		p::PageHeader header{reference.length, std::uint16_t(external ? 1 : 0), count,
							 reference.keys, reference.height, reference.digest};
		const auto encoded = p::encode_page_header(header);
		std::copy(encoded.begin(), encoded.end(), f->bytes.begin());
		std::copy(nodes.begin(), nodes.end(), f->bytes.begin() + p::page_header_bytes);
		if (external) {
			auto child = reference;
			child.keys = std::numeric_limits<std::uint64_t>::max();
			const auto content = p::encode_content(child);
			const auto placement = p::encode_placement(child);
			std::copy(content.begin(), content.end(), f->bytes.end() - p::child_bytes);
			std::copy(placement.begin(), placement.end(), f->bytes.end() - p::placement_bytes);
		}
		f->refresh();
		// Model framing changed after this exact mapped page was verified.
		x::VerifiedPages verified(f->bytes.size(), f->account);
		verified.set(reference.offset);
		f->inputs[0].pages = &verified;
		const auto hashes = p::hash_counts();
		rejects<std::invalid_argument>([&] {
			x::TrieOverlay overlay(f->account, f, f->inputs, f->root);
			change(overlay, f->rows.front());
		});
		check(p::hash_counts() == hashes);
	}
};
void malformed() {
	for (bool interior : {false, true}) {
		Graph g;
		const auto root = g.branch();
		const auto child = g.branch();
		g.slot(root, 0, child);
		g.slot(root, 1, child);
		g.slot(child, 0, interior ? child + 3 : child);
		g.slot(child, 1, child);
		g.refuse(2);
	}
	{
		Graph g;
		const auto root = g.branch();
		g.slot(root, 0, root);
		g.slot(root, 1, root);
		g.refuse(1);
	}
	{
		Graph g;
		const auto root = g.branch(), a = g.branch(), b = g.branch(), c = g.leaf();
		g.slot(root, 0, a);
		g.slot(root, 1, c);
		for (unsigned i = 0; i != 2; ++i) {
			g.slot(a, i, b);
			g.slot(b, i, c);
		}
		g.refuse(4);
	}
	{
		Graph g;
		const auto root = g.branch(), a = g.branch(std::string(202, 'x')), b = g.leaf();
		g.slot(root, 0, a);
		g.slot(root, 1, b);
		g.slot(a, 0, b);
		g.slot(a, 1, b);
		g.refuse(3);
	}
	{
		Graph g;
		const auto root = g.branch(), a = g.branch();
		g.slot(root, 0, a);
		g.slot(root, 1, a);
		g.slot(a, 0, 0x8000);
		g.slot(a, 1, 0x8000);
		g.refuse(2, true);
	}
}
} // namespace
int main() {
	control();
	malformed();
	std::cout << "dense overlay: exact prior values, verified-hit zero hashes, bounded malformed framing passed\n";
}
