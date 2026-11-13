#include "detent_distribution/grants.hh"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
namespace d = detent::distribution;
namespace {
void require(bool value, const char *what) {
	if (!value)
		throw std::runtime_error(what);
}
d::NamespaceGrants grants(std::initializer_list<std::string_view> patterns) {
	return d::NamespaceGrants(std::span<const std::string_view>(patterns.begin(), patterns.size()));
}
bool refused(std::initializer_list<std::string_view> patterns) {
	try {
		grants(patterns);
	} catch (const std::invalid_argument &) {
		return true;
	}
	return false;
}
} // namespace
int main() {
	try {
		// Exact names: that name only.
		auto exact = grants({"payments", "search.ranking"});
		require(exact.allows("payments") && exact.allows("search.ranking"), "exact names granted");
		require(!exact.allows("payments.eu") && !exact.allows("payment") && !exact.allows("paymentsx") &&
					!exact.allows("search") && !exact.allows("search.ranking.v2"),
				"an exact name grants nothing else");
		require(!exact.all() && !exact.empty(), "exact grants are neither all nor empty");

		// Whole-component prefixes: every name below `p.`, at any depth, never `p` itself.
		auto prefix = grants({"payments.*"});
		require(prefix.allows("payments.eu") && prefix.allows("payments.eu.cards") && prefix.allows("payments.x"),
				"a prefix grants every name below it");
		require(!prefix.allows("payments") && !prefix.allows("paymentsx") && !prefix.allows("payments_eu") &&
					!prefix.allows("payments-eu") && !prefix.allows("payments/eu") && !prefix.allows("pay") &&
					!prefix.allows("other.payments.eu"),
				"a prefix matches whole components only");
		auto nested = grants({"a.b.*", "z"});
		require(nested.allows("a.b.c") && !nested.allows("a.b") && !nested.allows("a.c") && !nested.allows("a.bc.d") &&
					nested.allows("z") && !nested.allows("z.y"),
				"a nested prefix matches below its last component");

		// `*`: every valid name.
		auto all = grants({"*"});
		require(all.all() && all.allows("a") && all.allows("payments.eu.cards") &&
					all.allows(std::string(198, 'n')),
				"`*` grants every name");
		require(!all.allows("") && !all.allows(std::string(199, 'n')) && !all.allows("bad name") &&
					!all.allows("a:b"),
				"no grant reaches an invalid name");

		// Nothing granted: no names.
		d::NamespaceGrants none;
		require(none.empty() && !none.allows("a"), "an empty grant grants nothing");
		require(grants({}).empty(), "no patterns grant nothing");

		// Mixed and duplicated patterns.
		auto mixed = grants({"b", "a.*", "b", "a.*", "c.d"});
		require(mixed.allows("b") && mixed.allows("a.x") && mixed.allows("c.d") && !mixed.allows("a") &&
					!mixed.allows("c"),
				"mixed patterns combine");

		// Only the three forms are accepted.
		for (auto bad : {"", "pay*", "*.payments", "a.*.b", ".*", "**", "a.**", "a*b", "a.b*", "* ", "a b",
						 "a:b", "*.*"})
			require(refused({bad}), bad);
		require(refused({std::string_view(std::string(199, 'n'))}), "a name over 198 bytes is refused");
		require(!refused({std::string_view(std::string(198, 'n'))}), "a 198-byte name is a valid grant");
		std::vector<std::string_view> many(d::NamespaceGrants::maximum_patterns + 1, "a");
		bool bounded = false;
		try {
			d::NamespaceGrants{std::span<const std::string_view>(many)};
		} catch (const std::invalid_argument &) {
			bounded = true;
		}
		require(bounded, "the pattern count is bounded");
	} catch (const std::exception &error) {
		std::cerr << "test_distribution_grants: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
