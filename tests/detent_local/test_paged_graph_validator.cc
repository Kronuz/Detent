#include "detent/hashing.hh"
#include "detent_local/paged_definition.hh"
#include "paged_graph_validator.hh"
#include "paged_segment_builder.hh"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("cold graph validation invariant");
}
template <class Error, class F> void rejects(F call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid page graph accepted");
}
struct Fixture {
	std::shared_ptr<d::StorageLedger> account = std::make_shared<d::StorageLedger>(4 * p::segment_bytes);
	std::shared_ptr<const d::OwnedBuffer> bytes;
	std::array<l::detail::SegmentInput, 1> segments;
	l::StorageIdentity storage{};
	p::Root root;
};
std::shared_ptr<Fixture> fixture(unsigned kind = 0) {
	auto value = std::make_shared<Fixture>();
	value->storage.fill('s');
	l::GenerationId publication{};
	publication.fill('g');
	l::detail::SegmentBuilder segment(value->account, value->storage, publication, 0);
	auto append = [&](p::PageKind type, std::string_view prefix, std::span<const p::Child> children,
					  std::span<const char> body) {
		l::detail::PageBuilder page(value->account, type, children.empty() ? 1 : 2, prefix, children, body);
		while (!page.done()) {
			l::ValidationBudget budget;
			page.step(budget);
		}
		segment.append(page);
		while (!segment.page_ready()) {
			l::ValidationBudget budget;
			segment.step(budget);
		}
		return segment.reference();
	};
	std::array<char, 8> watermark{};
	l::write_integer(watermark, 0, kind == 1 ? 7 : 9, 8);
	const auto name = kind == 7 ? std::string(198, 'n') : std::string("ns");
	const auto namespace_prefix = name + '\0';
	const auto ns = append(p::PageKind::Namespace, namespace_prefix, {}, watermark);
	detent::KnobDef source = detent::IntDef{42};
	if (kind == 5) {
		detent::BoolDef boolean;
		boolean.rollout_pct = 50;
		boolean.overrides.emplace("force", true);
		boolean.overrides.emplace("zero", detent::GroupRollout{0});
		boolean.overrides.emplace("full", detent::GroupRollout{100});
		boolean.overrides.emplace(std::string("a\0b", 3), true);
		source = boolean;
	}
	if (kind == 8) {
		detent::BoolDef boolean;
		boolean.default_value = true;
		for (unsigned i = 0; i < 256; ++i) {
			std::string key(128, 'x');
			key[0] = 'A' + i / 26;
			key[1] = 'a' + i % 26;
			boolean.overrides.emplace(std::move(key),
									  detent::GroupRollout{static_cast<std::uint8_t>(i % 101)});
		}
		source = boolean;
	}
	std::vector<char> definition(p::definition_size(source));
	p::encode_definition(definition, source, 8);
	const auto definition_prefix = (kind == 2 ? std::string("xx") : name) + '\0' + 'k' + '\0';
	const auto knob = append(p::PageKind::Definition, definition_prefix, {}, definition);
	std::array<p::Child, 2> children{{{'K', kind == 3 ? ns : knob}, {'W', ns}}};
	if (kind == 4)
		children[0].reference.offset = ns.offset + 8;
	const auto tree = append(p::PageKind::Branch, {}, children, {});
	segment.seal();
	l::ValidationBudget budget;
	segment.step(budget);
	value->bytes = segment.bytes();
	value->segments[0] = {segment.ownership(), {value->bytes->data(), value->bytes->size()}};
	auto &root = value->root;
	root.cursor.cluster.fill('c');
	root.cursor.group.fill('g');
	root.cursor.restore_epoch.fill('e');
	root.cursor.scope.fill('s');
	root.cursor.revision = 10;
	root.bytes = ns.length + knob.length + tree.length;
	root.pages = 3;
	root.records = root.namespaces = root.segments = 1;
	root.tree = tree;
	root.logical = tree.logical;
	return value;
}
void run(l::detail::GraphValidator &validator, std::size_t size = 1) {
	unsigned steps = 0;
	while (!validator.done()) {
		l::ValidationBudget budget{size, 1};
		validator.step(budget);
		check(budget.bytes <= size && budget.rows <= 1 && ++steps < 200000);
	}
}
void corrupt(std::shared_ptr<Fixture> value) {
	auto account = std::make_shared<d::StorageLedger>(4 * p::segment_bytes);
	l::detail::GraphValidator validator(account, value, value->segments, value->root, value->storage);
	rejects<std::invalid_argument>([&] { run(validator); });
	check(!validator.done());
	rejects<std::logic_error>([&] { validator.root(); });
	l::ValidationBudget budget;
	rejects<std::logic_error>([&] { validator.step(budget); });
}
} // namespace
int main() {
	auto value = fixture();
	auto account = std::make_shared<d::StorageLedger>(4 * p::segment_bytes);
	std::size_t admission;
	std::size_t peak;
	{
		l::detail::GraphValidator validator(account, value, value->segments, value->root, value->storage);
		admission = account->used();
		rejects<std::logic_error>([&] { validator.root(); });
		l::ValidationBudget empty{0, 0};
		validator.step(empty);
		check(!validator.done());
		run(validator);
		peak = account->peak();
		check(validator.root().logical == value->root.logical);
		check(!validator.discard(empty));
		l::ValidationBudget disposal{0, 1};
		check(validator.discard(disposal) && !account->used());
	}
	auto short_account = std::make_shared<d::StorageLedger>(admission - 1);
	rejects<d::StoragePressure>([&] {
		l::detail::GraphValidator validator(short_account, value, value->segments, value->root,
											value->storage);
	});
	check(!short_account->used());
	{
		auto short_work = std::make_shared<d::StorageLedger>(peak - 1);
		l::detail::GraphValidator validator(short_work, value, value->segments, value->root, value->storage);
		rejects<d::StoragePressure>([&] { run(validator); });
		check(!validator.done());
		l::ValidationBudget disposal{0, 1};
		check(validator.discard(disposal) && !short_work->used());
	}
	{
		auto empty = value->root;
		empty.bytes = empty.pages = empty.segments = empty.records = empty.namespaces = 0;
		empty.tree = {};
		empty.logical = p::empty_commitment();
		l::detail::GraphValidator validator(account, value, {}, empty, value->storage);
		run(validator);
		check(validator.root().pages == 0);
	}
	check(!account->used());
	for (unsigned kind = 1; kind <= 4; ++kind)
		corrupt(fixture(kind));
	auto changed = fixture();
	changed->root.records = 2;
	corrupt(changed);
	changed = fixture();
	++changed->root.bytes;
	corrupt(changed);
	changed = fixture();
	changed->segments[0].ownership.checksum[0] ^= 1;
	corrupt(changed);
	changed = fixture();
	changed->segments[0].ownership.storage[0] ^= 1;
	corrupt(changed);
	changed = fixture();
	p::Limits limits;
	limits.bytes = changed->root.bytes;
	{
		l::detail::GraphValidator validator(account, changed, changed->segments, changed->root,
											changed->storage, limits);
		rejects<std::invalid_argument>([&] { run(validator); });
	}
	check(!account->used());
	{
		d::RetirementRuntime runtime{account, 2};
		auto domain = runtime.domain(account, 16);
		auto retained = fixture();
		l::detail::GraphValidator validator(account, retained, retained->segments, retained->root,
											retained->storage);
		rejects<std::logic_error>([&] { validator.take(domain); });
		run(validator);
		auto short_controls = std::make_shared<d::StorageLedger>(4096);
		auto short_domain = runtime.domain(short_controls, 1);
		const auto domain_capacity = short_controls->used();
		auto pressure = short_controls->admit(short_controls->limit() - domain_capacity);
		rejects<d::StoragePressure>([&] { validator.take(short_domain); });
		pressure.reset();
		check(validator.done() && short_controls->used() == domain_capacity);
		short_domain.close().wait();
		auto view = validator.take(domain);
		retained.reset();
		check(!validator.done() && view.size() == 1 && view.int_count() == 1 && !view.bool_count());
		check(view.contains("ns", "k") && !view.contains("ns", "missing") && !view.contains("absent", "k"));
		check(view.get("ns", "k").value() == 42 && !view.eval("ns", "k"));
		check(!view.try_get("ns", "missing") && view.try_get("ns", "k")->has_value());
		view = {};
		domain.close().wait();
	}
	check(!account->used());
	for (auto kind : {5u, 7u, 8u}) {
		d::RetirementRuntime runtime{account, 1};
		auto domain = runtime.domain(account, 16);
		auto retained = fixture(kind);
		l::detail::GraphValidator validator(account, retained, retained->segments, retained->root,
											retained->storage);
		run(validator);
		auto view = validator.take(domain);
		retained.reset();
		if (kind == 7) {
			const auto name = std::string(198, 'n');
			check(view.get(name, "k").value() == 42 && !view.contains(name, "kk"));
		} else if (kind == 5) {
			check(view.bool_count() == 1 && !view.int_count());
			check(!view.eval("ns", "k").value());
			check(view.eval("ns", "k", {}, "force").value());
			check(view.eval("ns", "k", "host", "full").value());
			check(!view.eval("ns", "k", "host", "zero").value());
			check(view.eval("ns", "k", {}, std::string_view("a\0b", 3)).value());
			for (unsigned host = 0; host < 64; ++host) {
				const auto hash = std::to_string(host);
				check(view.eval("ns", "k", hash).value() == (detent::stable_bucket("ns:k", hash) < 50));
			}
		} else {
			std::string first(128, 'x'), last(128, 'x');
			first[0] = 'A';
			first[1] = 'a';
			last[0] = 'J';
			last[1] = 'v';
			check(view.eval("ns", "k", {}, first).value());
			check(!view.eval("ns", "k", "host", first).value());
			check(view.eval("ns", "k", "host", last).value() == (detent::stable_bucket("ns:k", "host") < 53));
			check(view.eval("ns", "k", "host", "missing").value());
		}
		view = {};
		domain.close().wait();
	}
	check(!account->used());
	std::cout << "cold graph hashes, contextual revisions, physical quotas and overlap rejection passed\n";
}
