#include "detent/hashing.hh"
#include "detent/model.hh"
#include "detent_local/paged_definition.hh"
#include "evaluation.hh"
#include <iostream>
#include <string>
#include <vector>

namespace d = detent;
namespace l = detent::local;
namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("evaluation invariant at line " + std::to_string(line));
}
#define CHECK(value) check(value, __LINE__)
// The reference semantics, written out independently of the shared evaluator: a matching group override
// (forced, or a group rollout that needs a hash), then the global rollout, then the default.
bool reference(const d::BoolDef &definition, std::string_view name, std::optional<std::string_view> hash,
               std::optional<std::string_view> group) {
	if (group) {
		const auto override = definition.overrides.find(*group);
		if (override != definition.overrides.end()) {
			if (const auto *forced = std::get_if<bool>(&override->second))
				return *forced;
			return hash ? d::stable_bucket(name, *hash) < std::get<d::GroupRollout>(override->second).rollout_pct
			            : definition.default_value;
		}
	}
	return definition.rollout_pct && hash ? d::stable_bucket(name, *hash) < *definition.rollout_pct
	                                      : definition.default_value;
}
// Evaluates through the one shared evaluator over a definition encoded as the paged format stores it.
bool lifted(const d::BoolDef &def, std::string_view ns, std::string_view key,
            std::optional<std::string_view> hash, std::optional<std::string_view> group) {
	std::vector<char> body(l::paged::definition_size(d::KnobDef(def)));
	l::paged::encode_definition(body, d::KnobDef(def), 1);
	return l::evaluate(l::detail::BorrowedDefinition::from_paged(body), ns, key, hash, group);
}
d::BoolDef make(bool fallback, std::optional<std::uint8_t> rollout,
                std::vector<std::pair<std::string, d::GroupOverride>> overrides = {}) {
	d::BoolDef def;
	def.default_value = fallback;
	def.rollout_pct = rollout;
	for (auto &[name, value] : overrides)
		def.overrides.emplace(name, value);
	return def;
}
} // namespace
int main() {
	// Pinned values.
	CHECK(lifted(make(true, {}), "ns", "k", {}, {}) == true);
	CHECK(lifted(make(false, {}), "ns", "k", "u", {}) == false);
	CHECK(lifted(make(true, 0), "ns", "k", {}, {}) == true);         // no hash: the default
	CHECK(lifted(make(true, 0), "ns", "k", "u", {}) == false);       // 0% ramps nobody
	CHECK(lifted(make(false, 100), "ns", "k", "u", {}) == true);     // 100% ramps everybody
	CHECK(lifted(make(false, 100), "ns", "k", {}, {}) == false);     // ...but only with a hash
	const auto forced = make(false, 100, {{"g", true}});
	CHECK(lifted(forced, "ns", "k", {}, "g") == true);               // forced wins with no hash
	CHECK(lifted(make(true, 100, {{"g", false}}), "ns", "k", "u", "g") == false);
	CHECK(lifted(make(true, 100, {{"g", false}}), "ns", "k", "u", "other") == true);
	CHECK(lifted(make(true, {}, {{"g", d::GroupRollout{0}}}), "ns", "k", "u", "g") == false);
	CHECK(lifted(make(false, {}, {{"g", d::GroupRollout{100}}}), "ns", "k", "u", "g") == true);
	CHECK(lifted(make(true, {}, {{"g", d::GroupRollout{0}}}), "ns", "k", {}, "g") == true);
	// A group rollout beats the global rollout: global 100 would say true.
	CHECK(lifted(make(false, 100, {{"g", d::GroupRollout{0}}}), "ns", "k", "u", "g") == false);
	// The bucket is stable_bucket("ns:k", hash), and the percentage is exclusive: bucket < pct.
	unsigned ramped = 0;
	for (unsigned i = 0; i != 400; ++i) {
		const auto hash = "user-" + std::to_string(i);
		const auto bucket = d::stable_bucket("ns:k", hash);
		for (std::uint8_t pct : {0, 1, 37, 50, 99, 100}) {
			CHECK(lifted(make(false, pct), "ns", "k", hash, {}) == (bucket < pct));
			CHECK(lifted(make(true, {}, {{"g", d::GroupRollout{pct}}}), "ns", "k", hash, "g") == (bucket < pct));
		}
		ramped += bucket < 50;
	}
	CHECK(ramped > 100 && ramped < 300);
	// Oracle: the reference semantics above on the full small domain.
	std::vector<std::optional<std::string>> hashes{std::nullopt}, groups{std::nullopt, "g", "other"};
	for (unsigned i = 0; i != 25; ++i)
		hashes.push_back("h" + std::to_string(i));
	std::vector<std::optional<std::uint8_t>> globals{std::nullopt, 0, 1, 50, 100};
	std::vector<std::vector<std::pair<std::string, d::GroupOverride>>> tables{
	    {}, {{"g", true}}, {{"g", false}}, {{"g", d::GroupRollout{0}}}, {{"g", d::GroupRollout{50}}},
	    {{"g", d::GroupRollout{100}}}};
	unsigned cases = 0;
	for (bool fallback : {false, true})
		for (auto global : globals)
			for (auto &table : tables)
				for (auto &group : groups)
					for (auto &hash : hashes) {
						const auto def = make(fallback, global, table);
						std::optional<std::string_view> h, g;
						if (hash)
							h = *hash;
						if (group)
							g = *group;
						CHECK(lifted(def, "ns", "k", h, g) == reference(def, "ns:k", h, g));
						++cases;
					}
	std::cout << cases << " cases\n";
	return 0;
}
