#include "export_scope.hh"
#include <array>
#include <iostream>
#include <vector>
namespace a = detent::authority;
namespace d = detent::distribution;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("authorized scope invariant");
}
template <class Function> void rejects(Function function) {
	bool rejected = false;
	try {
		function();
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	check(rejected);
}
} // namespace
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		std::set<std::string> allowed{"a", "ab", "b"};
		std::vector<std::string> requested{"b", "a", "b"};
		auto scope = a::AuthorizedScope::authorize("principal", 1, requested, allowed, account);
		check(scope->size() == 2 && scope->name(0) == "a" && scope->name(1) == "b");
		constexpr std::string_view expected =
			"1d601cea64e1f2c8c780c64a1939f78dbdb3665c613b1e67734671c6ce1bbf64";
		constexpr std::string_view hex = "0123456789abcdef";
		for (std::size_t i = 0; i < scope->identity().size(); ++i) {
			const auto value = static_cast<unsigned char>(scope->identity()[i]);
			check(expected[i * 2] == hex[value >> 4] && expected[i * 2 + 1] == hex[value & 15]);
		}
		requested = {"a", "b"};
		auto same = a::AuthorizedScope::authorize("principal", 1, requested, allowed, account);
		check(scope->identity() == same->identity());
		{
			std::array<std::string_view, 2> canonical{"a", "b"};
			std::array<std::string_view, 3> grants{"a", "ab", "b"};
			auto canonical_scope =
				a::AuthorizedScope::authorize_canonical("principal", 1, canonical, grants, account);
			check(canonical_scope->identity() == scope->identity());
			std::array<std::string_view, 2> reordered{"b", "a"};
			std::array<std::string_view, 2> duplicate{"a", "a"};
			std::array<std::string_view, 1> narrow{"a"};
			rejects(
				[&] { a::AuthorizedScope::authorize_canonical("principal", 1, reordered, grants, account); });
			rejects(
				[&] { a::AuthorizedScope::authorize_canonical("principal", 1, duplicate, grants, account); });
			rejects([&] {
				a::AuthorizedScope::authorize_canonical("principal", 1, canonical, reordered, account);
			});
			rejects(
				[&] { a::AuthorizedScope::authorize_canonical("principal", 1, canonical, narrow, account); });
		}
		auto principal = a::AuthorizedScope::authorize("other", 1, requested, allowed, account);
		auto epoch = a::AuthorizedScope::authorize("principal", 2, requested, allowed, account);
		check(scope->identity() != principal->identity() && scope->identity() != epoch->identity());
		requested = {"ab"};
		auto subset = a::AuthorizedScope::authorize("principal", 1, requested, allowed, account);
		check(scope->identity() != subset->identity());
		requested.clear();
		auto empty = a::AuthorizedScope::authorize("principal", 1, requested, allowed, account);
		check(empty->size() == 0 && empty->identity() != scope->identity());
		// Hash a separate framed representation that would collide under raw joins.
		requested = {"a", "b"};
		auto split = a::AuthorizedScope::authorize("p", 1, requested, allowed, account);
		requested = {"ab"};
		auto joined = a::AuthorizedScope::authorize("p", 1, requested, allowed, account);
		check(split->identity() != joined->identity());
		const auto before = account->used();
		for (const auto &name : {"secret", "a:b", "", "a b"}) {
			requested = {name};
			rejects([&] { a::AuthorizedScope::authorize("principal", 1, requested, allowed, account); });
			check(account->used() == before);
		}
		requested = {"a"};
		rejects([&] { a::AuthorizedScope::authorize("", 1, requested, allowed, account); });
		rejects([&] { a::AuthorizedScope::authorize("principal", 0, requested, allowed, account); });
		rejects([&] { a::AuthorizedScope::authorize("principal", 1, requested, allowed, account, 0); });
		bool refused = false;
		{
			auto pressure = account->admit(account->limit() - before);
			try {
				a::AuthorizedScope::authorize("principal", 1, requested, allowed, account);
			} catch (const d::StoragePressure &) {
				refused = true;
			}
			check(account->used() == account->limit());
		}
		check(refused && account->used() == before);
		std::weak_ptr<const a::AuthorizedScope> weak = scope;
		scope.reset();
		check(weak.expired());
		const auto retained = account->used();
		weak.reset();
		check(account->used() < retained);
		same.reset();
		principal.reset();
		epoch.reset();
		subset.reset();
		empty.reset();
		split.reset();
		joined.reset();
		check(account->used() == 0);
		std::cout << "canonical authorized scope and physical ownership passed\n";
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
