#include "paged_root_prefix.hh"
#include "paged_snapshot_builder.hh"
#include "snapshot_fixture.hh"
namespace l = detent::local;
namespace p = l::paged;
template <class Call> void rejects(Call call) {
	try {
		call();
	} catch (const std::invalid_argument &) {
		return;
	}
	throw std::runtime_error("inconsistent root prefix accepted");
}
int main() {
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	const auto encoded = p::encode_root(image.root());
	for (std::size_t n = 0; n <= encoded.size(); ++n) {
		const auto scope = l::detail::validate_paged_root_prefix(std::span(encoded).first(n));
		require(bool(scope) == (n >= 160));
		if (scope)
			require(scope->first == image.root().cursor && scope->second == image.root().profile);
	}
	for (const auto offset :
		 {0u, 7u, 8u, 11u, 12u, 15u, 48u, 55u, 144u, 147u, 148u, 151u, 152u, 158u, 280u, 319u}) {
		auto changed = encoded;
		changed[offset] ^= 1;
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(offset + 1)); });
	}
	{
		auto changed = encoded;
		changed[159] = 2;
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(160)); });
	}
	for (const auto identity : {64u, 80u, 96u, 112u, 160u, 184u, 216u}) {
		auto changed = encoded;
		const auto size = identity == 112 || identity >= 184 ? 32u : 16u;
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
		changed[248] ^= 1;
		rejects([&] { l::detail::validate_paged_root_prefix(std::span(changed).first(249)); });
	}
	p::Root empty;
	empty.cursor = cursor();
	empty.logical = p::empty_commitment();
	const auto blank = p::encode_root(empty);
	for (std::size_t n = 0; n <= blank.size(); ++n)
		l::detail::validate_paged_root_prefix(std::span(blank).first(n));
	for (const auto offset : {16u, 24u, 28u, 40u, 160u, 247u, 248u, 279u}) {
		auto changed = blank;
		changed[offset] ^= 1;
		rejects([&] {
			l::detail::validate_paged_root_prefix(std::span(changed).first(std::max(48u, offset + 1)));
		});
	}
	auto oversized = image.root();
	oversized.bytes = 512ull * 1024 * 1024;
	const auto larger = p::encode_root(oversized, l::detail::paged_format_limits());
	l::detail::validate_paged_root_prefix(larger);
	require(p::decode_root(larger, l::detail::paged_format_limits()).bytes == oversized.bytes);
	std::cout
		<< "all root truncations, visible contradictions, empty roots and quota-independent framing passed\n";
}
