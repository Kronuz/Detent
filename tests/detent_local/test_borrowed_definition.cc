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
	check(view.default_value() == definition.default_value && view.rollout() == definition.rollout_pct);
	check(view.rollout_value() == definition.rollout_value.value_or(definition.default_value));
	for (const auto &[key, expected] : definition.overrides)
		check(view.override_for(key) == expected);
	for (auto absent : {"", "A", "missing", "zzzzzz", "\xff"})
		check(!view.override_for(absent));
}
} // namespace
int main() {
	for (auto value : {INT64_MIN, INT64_MAX, std::int64_t(-1), std::int64_t(0)})
		qualify(detent::KnobDef{value});
	detent::KnobDef knob;
	knob.default_value = 1;
	knob.rollout_pct = 0;
	knob.rollout_value = INT64_MIN;
	qualify(knob);
	knob.rollout_pct = 100;
	knob.overrides.emplace(std::string("a\0b", 3), std::int64_t(0));
	knob.overrides.emplace("beta", detent::GroupRollout{17});
	knob.overrides.emplace("\xc3\xa9", INT64_MAX);
	qualify(knob);
	knob.overrides.clear();
	knob.rollout_pct.reset();
	for (unsigned i = 0; i < 256; ++i) {
		std::string key(128, 'x');
		key[0] = 'a' + i / 26;
		key[1] = 'a' + i % 26;
		knob.overrides.emplace(std::move(key), detent::GroupRollout{static_cast<std::uint8_t>(i % 101)});
	}
	qualify(knob);
	std::cout << "borrowed canonical definitions preserve signed defaults, rollouts and opaque override lookup\n";
}
