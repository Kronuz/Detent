#include "paged_leaf_body.hh"
#include <iostream>
#include <vector>
namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
namespace {
void require(bool condition) {
	if (!condition)
		throw std::runtime_error("leaf body invariant failed");
}
template <class E, class F> void rejects(F call) {
	try {
		call();
	} catch (const E &) {
		return;
	}
	throw std::runtime_error("invalid leaf body accepted");
}
p::Override row(const void *context, std::uint64_t ordinal, std::uint32_t index) {
	require(ordinal == 17);
	return static_cast<const std::vector<p::Override> *>(context)->at(index);
}
void encode(l::detail::LeafBody &body) {
	for (unsigned turn = 0; !body.done(); ++turn) {
		require(turn < 200000);
		l::ValidationBudget budget{1, 1};
		body.step(budget);
	}
}
void discard(l::detail::LeafBody &body) {
	for (unsigned turn = 0;; ++turn) {
		require(turn < 200000);
		l::ValidationBudget budget{1, 1};
		if (body.discard(budget))
			return;
	}
}
} // namespace
int main() {
	auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
	std::vector<p::Override> rows{{"beta", false, 1}, {"tést", true, 17}};
	p::Definition definition;
	definition.revision = 42;
	definition.boolean = true;
	definition.overrides = rows.size();
	{
		l::detail::LeafBody body(account, definition, &rows, 17, row, p::page_bytes);
		rejects<std::logic_error>([&] { body.bytes(); });
		l::ValidationBudget zero{0, 0};
		body.step(zero);
		require(!body.done() && account->used() == 0);
		encode(body);
		auto bytes = body.bytes();
		require(p::validate_definition(std::span<const char>(bytes->data(), bytes->size()), 42).overrides ==
				2);
		bytes.reset();
		require(!body.discard(zero));
		discard(body);
	}
	require(account->used() == 0);
	{
		l::detail::LeafBody body(account, std::uint64_t{0});
		encode(body);
		require(body.bytes()->size() == 8 &&
				l::read_integer(std::span<const char>(body.bytes()->data(), body.bytes()->size()), 0, 8) ==
					0);
		discard(body);
	}
	for (const auto bad : {std::string_view("beta"), std::string_view("alpha"), std::string_view("\0", 1)}) {
		rows[1].key = bad;
		l::detail::LeafBody body(account, definition, &rows, 17, row, p::page_bytes);
		rejects<std::invalid_argument>([&] { encode(body); });
		discard(body);
		require(account->used() == 0);
	}
	rows = {{"a", false, 1}};
	definition.overrides = 1;
	{
		l::detail::LeafBody body(account, definition, &rows, 17, row, p::page_bytes);
		l::ValidationBudget count{5, 1};
		body.step(count);
		rows[0].key = "longer";
		rejects<std::length_error>([&] { encode(body); });
		discard(body);
	}
	{
		l::detail::LeafBody body(account, definition, &rows, 17, row, 24);
		rejects<std::length_error>([&] { encode(body); });
		discard(body);
	}
	{
		auto pressure = account->admit(account->limit());
		l::detail::LeafBody body(account, std::uint64_t{1});
		rejects<d::StoragePressure>([&] { encode(body); });
		discard(body);
	}
	require(account->used() == 0);
	std::cout << "bounded leaf encoding, canonical overrides, changing sources and cleanup passed\n";
}
