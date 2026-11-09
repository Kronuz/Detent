#include "dense_fixture.hh"
#include "paged_root_prefix.hh"
#include <iostream>
using namespace dense_test;
int main() {
	auto root = empty_root();
	const auto page = watermark();
	root.namespaces = root.pages = root.segments = 1;
	root.bytes = page.size();
	root.tree = reference(page);
	const auto encoded = p::encode_root(root, p::Limits{});
	for (std::size_t n = 0; n <= encoded.size(); ++n) {
		const auto scope = l::detail::validate_paged_root_prefix(std::span(encoded).first(n));
		require(bool(scope) == (n >= 160));
		if (scope)
			require(scope->first == root.cursor && scope->second == root.profile);
	}
	for (const auto offset :
		 {0u, 7u, 8u, 11u, 12u, 15u, 48u, 55u, 144u, 147u, 148u, 151u, 152u, 158u, 225u, 319u}) {
		auto changed = encoded;
		changed[offset] ^= 1;
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(offset + 1)); });
	}
	{
		auto changed = encoded;
		changed[159] = 2;
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(160)); });
	}
	for (const auto identity : {64u, 80u, 96u, 112u, 160u, 193u}) {
		auto changed = encoded;
		const auto size = identity == 112 || identity == 193 ? 32u : 16u;
		std::fill_n(changed.begin() + identity, size, 0);
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(identity + size)); });
	}
	for (const auto field : {16u, 32u, 40u}) {
		auto changed = encoded;
		l::write_integer(changed, field, 0, 8);
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(48)); });
	}
	{
		auto changed = encoded;
		changed[179] |= 1;
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(180)); });
		changed = encoded;
		l::write_integer(changed, 180, p::page_bytes + 1, 4);
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(184)); });
		changed = encoded;
		l::write_integer(changed, 184, 2, 8);
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(192)); });
		changed = encoded;
		changed[192] = 0;
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(193)); });
		changed = encoded;
		changed[7] = '2';
		changed[11] = 2;
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(16)); });
	}
	const auto blank = p::encode_root(empty_root(), p::Limits{});
	for (std::size_t n = 0; n <= blank.size(); ++n)
		l::detail::validate_paged_root_prefix(std::span(blank).first(n));
	for (const auto offset : {16u, 24u, 28u, 40u, 160u, 224u, 225u, 248u, 319u}) {
		auto changed = blank;
		changed[offset] ^= 1;
		rejects([&] {
			l::detail::validate_paged_root_prefix(std::span(changed).first(std::max(48u, offset + 1)));
		});
	}
	{
		auto dense = root;
		dense.records = 10;
		dense.tree.keys = 11;
		dense.bytes = dense.tree.length = 1024;
		const auto bytes = p::encode_root(dense, p::Limits{});
		for (std::size_t n = 0; n <= bytes.size(); ++n)
			l::detail::validate_paged_root_prefix(std::span(bytes).first(n));
	}
	auto oversized = root;
	oversized.bytes = 512ull * 1024 * 1024;
	const auto larger = p::encode_root(oversized, l::detail::paged_format_limits());
	l::detail::validate_paged_root_prefix(larger);
	require(p::decode_root(larger, l::detail::paged_format_limits()).bytes == oversized.bytes);
	std::cout << "dense root prefixes: all truncations, summaries, empty roots, reserved bytes and old-format refusal passed\n";
}
