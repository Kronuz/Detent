// A delta's overlay finds a page it already removed through an index: the extent comparisons it spends
// per changed key grow with the logarithm of the pages touched, not with their number.
#include "detent_local/paged_definition.hh"
#include "paged_disk_fixture.hh"
#include "paged_trie_overlay.hh"
#include <cstdio>
#include <iostream>
namespace p = l::paged;
namespace {
std::string name(unsigned i) {
	char text[16];
	std::snprintf(text, sizeof text, "app:n%06u", i);
	return text;
}
// A snapshot of `count` integer definitions in one namespace, each at revision 42.
std::shared_ptr<const d::ValidatedSnapshot> snapshot(unsigned count) {
	std::string wire;
	auto a = [&](unsigned v) { m::array(wire, v); };
	auto n = [&](std::uint64_t v) { m::unsigned_integer(wire, v); };
	a(7);
	n(1);
	n(1);
	a(2);
	n(1);
	n(1);
	a(5);
	const auto id = cursor();
	for (const auto *f : {&id.cluster, &id.group, &id.restore_epoch})
		m::string(wire, {f->data(), f->size()}, true);
	m::string(wire, {id.scope.data(), id.scope.size()}, true);
	n(42);
	a(4);
	n(1);
	n(count);
	n(0);
	n(3 + std::uint64_t(count) * name(0).size());
	a(1);
	a(2);
	m::string(wire, "app");
	n(42);
	a(count);
	for (unsigned i = 0; i < count; ++i) {
		a(4);
		n(0);
		m::string(wire, name(i));
		n(42);
		a(4);
		m::signed_integer(wire, i);
		wire.push_back('\xc0');
		wire.push_back('\xc0');
		a(0);
	}
	return std::make_shared<const d::ValidatedSnapshot>(decode(wire));
}
struct Input {
	l::detail::PagedImage image;
	std::vector<l::detail::SegmentInput> segments;
	explicit Input(l::detail::PagedImage output) : image(std::move(output)) {
		for (const auto &s : image.segments())
			segments.push_back({s.ownership, {s.bytes->data(), s.bytes->size()}});
		std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
		});
	}
};
struct Probe {
	std::uint64_t keys = 0, pages = 0, probes = 0;
	double per_key() const { return double(probes) / double(keys); }
};
// Changes every definition of a `count`-definition store, as a 100% block answer does.
Probe change_all(unsigned count) {
	auto source = snapshot(count);
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, generation);
	while (!builder.done()) {
		l::ValidationBudget b;
		builder.step(b);
	}
	auto input = std::make_shared<Input>(builder.take());
	Probe probe;
	{
		l::detail::TrieOverlay overlay(source->storage_account(), input, input->segments, input->image.root());
		for (unsigned i = 0; i < count; ++i) {
			std::array<char, 202> key{};
			const auto length = p::encode_definition_key(key, name(i));
			const detent::KnobDef value{std::int64_t(i) + 1};
			std::vector<char> body(p::definition_size(value));
			p::encode_definition(body, value, 43);
			overlay.change({key.data(), length}, std::span<const char>(body));
			while (!overlay.operation_done()) {
				l::ValidationBudget b;
				overlay.step(b);
			}
			require(overlay.changed());
		}
		overlay.seal();
		for (const auto &s : overlay.removed())
			probe.pages += s.pages;
		require(probe.pages == input->image.root().pages && overlay.touched_pages().size() == probe.pages);
		probe.keys = count;
		probe.probes = overlay.removal_probes();
		for (;;) {
			l::ValidationBudget b;
			if (overlay.discard(b))
				break;
		}
	}
	std::cout << count << " keys: " << probe.pages << " pages removed, " << probe.probes << " probes, "
			  << probe.per_key() << " per key\n";
	return probe;
}
} // namespace
int main() {
	const auto small = change_all(2000), large = change_all(32000);
	// Sixteen times the keys touch sixteen times the pages: a linear search costs about sixteen times
	// the comparisons per key (178 against 11.7), the index under three (23.3 against 8.4).
	require(large.pages >= 8 * small.pages);
	require(large.per_key() <= 4 * small.per_key());
}
