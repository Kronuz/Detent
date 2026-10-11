#include "detent_local/paged_definition.hh"
#include "paged_disk_fixture.hh"
#include "paged_navigation.hh"
#include "paged_trie_overlay.hh"
#include <map>
namespace p = l::paged;
using Rows = std::map<std::string, std::pair<detent::KnobDef, std::uint64_t>>;
std::shared_ptr<const d::ValidatedSnapshot> oracle(const Rows &rows) {
	std::string wire;
	auto a = [&](unsigned v) { m::array(wire, v); };
	auto n = [&](std::uint64_t v) { m::unsigned_integer(wire, v); };
	auto text = [&](std::string_view v) { m::string(wire, v); };
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
	std::uint64_t strings = 3, overrides = 0;
	for (const auto &[key, row] : rows) {
		strings += key.size();
		if (auto b = std::get_if<detent::BoolDef>(&row.first))
			for (const auto &[name, v] : b->overrides) {
				++overrides;
				strings += name.size();
			}
	}
	a(4);
	n(1);
	n(rows.size());
	n(overrides);
	n(strings);
	a(1);
	a(2);
	text("app");
	n(42);
	a(rows.size());
	for (const auto &[key, row] : rows) {
		a(4);
		n(0);
		text(key);
		n(row.second);
		if (auto b = std::get_if<detent::BoolDef>(&row.first)) {
			a(4);
			n(0);
			wire.push_back(b->default_value ? '\xc3' : '\xc2');
			if (b->rollout_pct)
				n(*b->rollout_pct);
			else
				wire.push_back('\xc0');
			a(b->overrides.size());
			for (const auto &[name, v] : b->overrides) {
				a(3);
				text(name);
				if (auto pct = std::get_if<detent::GroupRollout>(&v)) {
					n(1);
					n(pct->rollout_pct);
				} else {
					n(0);
					wire.push_back(std::get<bool>(v) ? '\xc3' : '\xc2');
				}
			}
		} else {
			a(2);
			n(1);
			m::signed_integer(wire, std::get<detent::IntDef>(row.first).default_value);
		}
	}
	return std::make_shared<const d::ValidatedSnapshot>(decode(wire));
}
struct Input {
	std::shared_ptr<const d::ValidatedSnapshot> source;
	l::detail::PagedImage image;
	std::vector<l::detail::SegmentInput> segments;
	Input(std::shared_ptr<const d::ValidatedSnapshot> rows, l::detail::PagedImage output)
		: source(std::move(rows)), image(std::move(output)) {
		for (const auto &s : image.segments())
			segments.push_back({s.ownership, {s.bytes->data(), s.bytes->size()}});
		std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
		});
	}
};
std::pair<p::Digest, std::uint64_t> fingerprint(l::detail::OverlayReference ref,
												std::span<const l::detail::SegmentInput> segments) {
	if (!ref)
		return {p::empty_commitment(), 0};
	if (ref.old) {
		const auto r = p::decode_reference({ref.old, p::reference_bytes});
		const auto bytes =
			l::detail::checked_page_bytes(segments[l::detail::find_segment(segments, r.segment)], r);
		return {r.logical, p::decode_page_header(bytes).keys};
	}
	const auto &node = *ref.fresh;
	std::vector<p::Child> children;
	std::uint64_t keys = 0;
	for (const auto &child : node.children) {
		const auto [logical, count] = fingerprint(child.reference, segments);
		p::Reference r;
		r.logical = logical;
		children.push_back({child.label, r});
		keys += count;
	}
	if (node.kind != p::PageKind::Branch)
		keys = 1;
	require(node.keys == keys);
	return {p::logical_commitment(node.kind, node.prefix, children, node.body), keys};
}
void finish(l::detail::TrieOverlay &overlay) {
	unsigned turns = 0;
	while (!overlay.operation_done()) {
		l::ValidationBudget b{1, 1};
		overlay.step(b);
		require(++turns < 1000000 && b.bytes <= 1 && b.rows <= 1);
	}
}
int main() {
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget b;
		builder.step(b);
	}
	auto input = std::make_shared<Input>(source, builder.take());
	{
		l::detail::GraphValidator validator(source->storage_account(), input, input->segments,
											input->image.root(), storage);
		while (!validator.done()) {
			l::ValidationBudget b;
			validator.step(b);
		}
	}
	detent::BoolDef boolean;
	boolean.default_value = true;
	boolean.rollout_pct = 25;
	boolean.overrides.emplace("beta", detent::GroupRollout{17});
	Rows original{{"app:count", {detent::IntDef{INT64_MIN}, 41}}, {"app:flag", {boolean, 42}}};
	struct Change {
		std::string key;
		std::optional<std::int64_t> value;
	};
	const std::vector<Change> changes{{"app:" + std::string(196, 'x'), 13},
									  {"app:" + std::string(196, 'x'), {}},
									  {"app:delta", 1},
									  {"app:echo", 2},
									  {"app:zeta", 3},
									  {"app:delta", {}},
									  {"app:echo", {}},
									  {"app:zeta", {}},
									  {"app:count", 7},
									  {"app:counter", 8},
									  {"app:c", 9},
									  {"app:co", 10},
									  {"app:flag", {}},
									  {"app:missing", {}},
									  {"app:counter", {}},
									  {"app:co", {}},
									  {"app:c", {}},
									  {"app:count", {}}};
	{
		const auto baseline = source->storage_account()->used();
		p::Root empty;
		empty.cursor = cursor();
		empty.logical = p::empty_commitment();
		l::detail::TrieOverlay overlay(source->storage_account(), input, {}, empty);
		std::array<char, 202> key{};
		const auto length = p::encode_namespace_key(key, "app");
		std::array<char, 8> body{};
		l::write_integer(body, 0, 42, 8);
		overlay.change({key.data(), length}, body);
		finish(overlay);
		require(overlay.changed());
		overlay.change({key.data(), length}, {});
		finish(overlay);
		require(overlay.previous_body().has_value());
		overlay.seal();
		require(!overlay.root());
		for (;;) {
			l::ValidationBudget b{1, 1};
			if (overlay.discard(b))
				break;
		}
		require(source->storage_account()->used() == baseline);
	}

	{
		const auto baseline = source->storage_account()->used();
		{
			l::detail::TrieOverlay overlay(source->storage_account(), input, input->segments,
										   input->image.root());
			std::array<char, 202> key{};
			const auto length = p::encode_definition_key(key, "app:count");
			std::array<char, 24> body{};
			p::encode_definition(body, detent::IntDef{0}, 42);
			overlay.change({key.data(), length}, body);
			l::ValidationBudget partial{sizeof(l::detail::OverlayNode) + body.size() + 1, 3};
			overlay.step(partial);
			require(!overlay.operation_done());
			l::ValidationBudget none{0, 0};
			require(!overlay.discard(none));
			unsigned turns = 0;
			for (;;) {
				l::ValidationBudget b{1, 1};
				++turns;
				if (overlay.discard(b))
					break;
				require(turns < 100000);
			}
			require(turns >= 8192);
		}
		require(source->storage_account()->used() == baseline);
		auto pressure = source->storage_account()->admit(source->storage_account()->limit() - baseline);
		bool rejected = false;
		try {
			l::detail::TrieOverlay overlay(source->storage_account(), input, input->segments,
										   input->image.root());
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
	}

	for (std::size_t end = 0; end <= changes.size(); ++end) {
		const auto baseline = source->storage_account()->used();
		{
			l::detail::TrieOverlay overlay(source->storage_account(), input, input->segments,
										   input->image.root());
			auto expected = original;
			for (std::size_t i = 0; i < end; ++i) {
				const auto &change = changes[i];
				std::array<char, 202> key{};
				const auto length = p::encode_definition_key(key, change.key);
				std::vector<char> body;
				if (change.value) {
					detent::KnobDef value = detent::IntDef{*change.value};
					body.resize(p::definition_size(value));
					p::encode_definition(body, value, 42);
					expected[change.key] = {value, 42};
				} else
					expected.erase(change.key);
				overlay.change({key.data(), length},
							   change.value ? std::optional<std::span<const char>>(body) : std::nullopt);
				finish(overlay);
				require(overlay.changed() == (change.key != "app:missing"));
			}
			overlay.seal();
			const auto [logical, keys] = fingerprint(overlay.root(), input->segments);
			auto rows = oracle(expected);
			l::detail::PagedSnapshotBuilder full(rows, storage, generation);
			while (!full.done()) {
				l::ValidationBudget b;
				full.step(b);
			}
			auto image = full.take();
			require(logical == image.root().logical && keys == expected.size() + 1);
			std::uint64_t removed_pages = 0, removed_bytes = 0;
			for (const auto &s : overlay.removed()) {
				removed_pages += s.pages;
				removed_bytes += s.bytes;
			}
			require(removed_pages <= input->image.root().pages && removed_bytes <= input->image.root().bytes);
			for (;;) {
				l::ValidationBudget b{1, 1};
				if (overlay.discard(b))
					break;
			}
		}
		require(source->storage_account()->used() == baseline);
	}
}
