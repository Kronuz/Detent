#include "detent/hashing.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_distribution/protocol.hh"
#include "detent_local/builder.hh"
#include <iostream>
namespace d = detent::distribution;
namespace m = d::messagepack;
void require(bool value) {
	if (!value)
		throw std::runtime_error("snapshot invariant failed");
}
d::Cursor cursor() {
	d::Cursor value;
	value.cluster.fill('c');
	value.group.fill('g');
	value.restore_epoch.fill('e');
	value.scope.fill('s');
	value.revision = 42;
	return value;
}
std::string fixture(std::uint64_t capabilities = 1, std::uint64_t strings = 24, std::uint64_t percentage = 25,
					std::uint64_t revision = 42) {
	std::string out;
	auto n = [&](std::uint64_t value) { m::unsigned_integer(out, value); };
	auto a = [&](std::uint32_t value) { m::array(out, value); };
	auto s = [&](std::string_view value) { m::string(out, value); };
	a(7);
	n(1);
	n(1);
	a(2);
	n(1);
	n(capabilities);
	a(5);
	const auto id = cursor();
	for (const auto *field : {&id.cluster, &id.group, &id.restore_epoch})
		m::string(out, std::string_view(field->data(), field->size()), true);
	m::string(out, std::string_view(id.scope.data(), id.scope.size()), true);
	n(revision);
	a(4);
	n(1);
	n(2);
	n(1);
	n(strings);
	a(1);
	a(2);
	s("app");
	n(42);
	a(2);
	a(4);
	n(0);
	s("app:count");
	n(41);
	a(2);
	n(1);
	m::signed_integer(out, std::numeric_limits<std::int64_t>::min());
	a(4);
	n(0);
	s("app:flag");
	n(42);
	a(4);
	n(0);
	out.push_back('\xc3');
	n(percentage);
	a(1);
	a(3);
	s("beta");
	n(1);
	n(17);
	return out;
}
d::ValidatedSnapshot decode(std::string_view bytes, d::Cursor expected = cursor()) {
	d::SnapshotDecoder decoder(expected, {"app"});
	auto input = std::span(bytes.data(), bytes.size());
	d::WorkBudget zero{0, 0};
	decoder.feed(input, zero);
	require(input.size() == bytes.size());
	while (!input.empty()) {
		d::WorkBudget budget{1, 1};
		decoder.feed(input, budget);
	}
	return decoder.finish();
}
int main() {
	try {
		auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
		detent::local::GenerationId generation;
		generation.fill('a');
		detent::local::ImageBuilder builder(snapshot, generation);
		std::size_t turns = 0;
		require(!builder.done());
		bool rejected = false;
		try {
			builder.bytes();
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		while (!builder.done()) {
			detent::local::ValidationBudget budget{1, 1};
			builder.step(budget);
			require(++turns < 5000);
		}
		auto bytes = builder.bytes();
		auto descriptor = builder.descriptor();
		require(descriptor.length == bytes->size() && descriptor.cursor == cursor());
		detent::local::ImageValidator validator(bytes, std::span<const char>(*bytes));
		while (!validator.done()) {
			detent::local::ValidationBudget budget{1, 1};
			validator.step(budget);
		}
		auto view = validator.take();
		require(view.get("app", "count").value() == std::numeric_limits<std::int64_t>::min());
		require(view.eval("app", "flag").value());
		require(view.eval("app", "flag", {}, "beta").value());
		require(view.eval("app", "flag", "host", "beta").value() ==
				(detent::stable_bucket("app:flag", "host") < 17));
		require(view.eval("app", "flag", "host").value() == (detent::stable_bucket("app:flag", "host") < 25));
		detent::local::ViewLimits limits;
		limits.bytes = 192;
		rejected = false;
		try {
			detent::local::ImageBuilder too_large(snapshot, generation, limits);
			while (!too_large.done()) {
				detent::local::ValidationBudget b;
				too_large.step(b);
			}
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		std::cout << "snapshot to sealed image to pinned view passed\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
