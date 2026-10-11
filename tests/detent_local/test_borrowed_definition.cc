#include "borrowed_definition.hh"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace p = detent::local::paged;
namespace l = detent::local;
namespace {
void check(bool ok) {
	if (!ok)
		throw std::runtime_error("borrowed definition semantic mismatch");
}
void qualify(const detent::KnobDef &definition) {
	std::vector<char> body(p::definition_size(definition));
	p::encode_definition(body, definition, 42);
	p::validate_definition(body, 42);
	const auto view = l::detail::BorrowedDefinition::from_paged(body);
	check(view.qualified_key().empty());
	if (const auto *integer = std::get_if<detent::IntDef>(&definition)) {
		check(!view.boolean() && view.default_int() == integer->default_value && !view.rollout());
		check(!view.override_for("any"));
		return;
	}
	const auto &boolean = std::get<detent::BoolDef>(definition);
	check(view.boolean() && view.default_bool() == boolean.default_value &&
		  view.rollout() == boolean.rollout_pct);
	for (const auto &[key, expected] : boolean.overrides)
		check(view.override_for(key) == expected);
	for (auto absent : {"", "A", "missing", "zzzzzz", "\xff"})
		check(!view.override_for(absent));
}
} // namespace
int main() {
	for (auto value : {INT64_MIN, INT64_MAX, std::int64_t(-1), std::int64_t(0)})
		qualify(detent::IntDef{value});
	detent::BoolDef boolean;
	qualify(boolean);
	boolean.default_value = true;
	boolean.rollout_pct = 0;
	qualify(boolean);
	boolean.rollout_pct = 100;
	boolean.overrides.emplace(std::string("a\0b", 3), false);
	boolean.overrides.emplace("beta", detent::GroupRollout{17});
	boolean.overrides.emplace("\xc3\xa9", true);
	qualify(boolean);
	boolean.overrides.clear();
	for (unsigned i = 0; i < 256; ++i) {
		std::string key(128, 'x');
		key[0] = 'a' + i / 26;
		key[1] = 'a' + i % 26;
		boolean.overrides.emplace(std::move(key), detent::GroupRollout{static_cast<std::uint8_t>(i % 101)});
	}
	qualify(boolean);
	std::cout
		<< "borrowed canonical definitions preserve signed defaults, rollouts and opaque override lookup\n";
}
