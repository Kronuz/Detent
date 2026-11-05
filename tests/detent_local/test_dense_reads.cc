#include "dense_graph_fixture.hh"
#include "paged_navigation.hh"
#include "paged_read_state.hh"
#include <iostream>
using namespace dense_graph_test;
namespace {
l::detail::PagedReadState read_state(const std::shared_ptr<Fixture> &source,
                                     const l::detail::VerifiedPages &verified) {
	source->inputs[0].pages = &verified;
	return {source->root, source->inputs};
}
void mutation(unsigned kind) {
	auto source = fixture(1000);
	l::detail::VerifiedPages verified(source->bytes.size(), source->account);
	auto state = read_state(source, verified);
	check(state.lookup(source->rows.front().key, p::PageKind::Definition).has_value());
	const auto offset = source->root.tree.offset;
	auto root = std::span<char>(source->bytes).subspan(offset, source->root.tree.length);
	const auto node = p::decode_node(root, p::page_header_bytes);
	if (kind == 0)
		l::write_integer(root, 12, 0x7fff, 2);
	else if (kind == 1)
		root[64] = 0;
	else if (kind == 2)
		l::write_integer(root, node.slots.data() - root.data(), 64, 2);
	else if (kind == 3)
		root[14] = char(0xff);
	else
		root[24] ^= 1;
	rejects<l::detail::ReadSourceFailure>(
	    [&] { state.lookup(source->rows.front().key, p::PageKind::Definition); });
}
} // namespace
int main() {
	{
		auto account = std::make_shared<d::StorageLedger>(1024);
		l::detail::VerifiedPages verified(160, account);
		verified.set(0);
		check(verified.test(0) && !verified.test(8) && !verified.test(1));
		rejects<std::invalid_argument>([&] { verified.set(8); });
		verified.set(80);
		check(verified.test(80) && !verified.test(88) && !verified.test(10000));
	}
	for (auto count : {1u, 32u, 1000u}) {
		auto source = fixture(count);
		l::detail::VerifiedPages verified(source->bytes.size(), source->account);
		auto state = read_state(source, verified);
		const auto before = p::hash_counts().page_calls;
		for (const auto &row : source->rows) {
			const auto expected = p::decode_key(row.key).kind;
			const auto body = state.lookup(row.key, expected);
			check(body && std::equal(body->begin(), body->end(), row.body.begin(), row.body.end()));
		}
		const auto warmed = p::hash_counts().page_calls;
		check(warmed == before + source->pages.size());
		for (const auto &row : source->rows)
			check(state.lookup(row.key, p::decode_key(row.key).kind).has_value());
		check(p::hash_counts().page_calls == warmed);
		check(!state.definition("ns", "absent"));
	}
	{
		auto source = fixture(1000);
		check(source->pages.size() > 2);
		l::detail::VerifiedPages verified(source->bytes.size(), source->account);
		source->inputs[0].pages = &verified;
		const auto &target = source->pages.front().reference;
		const auto &other = source->pages.back().reference;
		l::detail::checked_page_bytes(source->inputs[0], target);
		l::detail::checked_page_bytes(source->inputs[0], other);
		auto redirected = other;
		redirected.offset = target.offset;
		redirected.length = target.length;
		rejects<l::detail::ReadSourceFailure>(
		    [&] { l::detail::checked_page_bytes(source->inputs[0], redirected); });
		redirected = target;
		redirected.offset += 8;
		rejects<std::exception>(
		    [&] { l::detail::checked_page_bytes(source->inputs[0], redirected); });
	}
	{
		auto source = fixture(1);
		l::detail::VerifiedPages verified(source->bytes.size(), source->account);
		auto state = read_state(source, verified);
		const auto body = state.lookup(source->rows.front().key, p::PageKind::Definition);
		check(body && body->size() >= 24);
		const auto at = body->data() - source->bytes.data();
		// Value edits retaining valid framing remain visible until scrub.
		source->bytes[at + 23] ^= 1;
		const auto changed = state.lookup(source->rows.front().key, p::PageKind::Definition);
		check(changed && (*changed)[23] != source->rows.front().body[23]);
		auto bytes = std::span<char>(source->bytes)
		                 .subspan(source->root.tree.offset, source->root.tree.length);
		std::size_t node_at = p::page_header_bytes;
		for (;;) {
			const auto node = p::decode_node(bytes, node_at);
			if (node.body.data() == body->data())
				break;
			node_at = node.end;
		}
		l::write_integer(bytes, node_at + 2, UINT16_MAX, 2);
		rejects<l::detail::ReadSourceFailure>(
		    [&] { state.lookup(source->rows.front().key, p::PageKind::Definition); });
	}
	for (unsigned kind = 0; kind < 5; ++kind)
		mutation(kind);
	std::cout << "dense reads, one-hash first touch, exact starts and mapped "
	             "mutation bounds passed\n";
}
