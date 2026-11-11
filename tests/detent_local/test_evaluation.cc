#include "detent/detail/evaluation.hh"
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
// Evaluates through the paged evaluator over a definition encoded as the paged format stores it.
std::int64_t lifted(const d::KnobDef &def, std::string_view ns, std::string_view key,
                    std::optional<std::string_view> hash, std::optional<std::string_view> group) {
	std::vector<char> body(l::paged::definition_size(def));
	l::paged::encode_definition(body, def, 1);
	return l::evaluate(l::detail::BorrowedDefinition::from_paged(body), ns, key, hash, group);
}
// A definition with a rollout value exactly when some percentage exists.
d::KnobDef make(std::int64_t fallback, std::optional<std::uint8_t> rollout,
                std::vector<std::pair<std::string, d::GroupOverride>> overrides = {}, std::int64_t value = 1) {
	d::KnobDef def;
	def.default_value = fallback;
	def.rollout_pct = rollout;
	bool percentage = rollout.has_value();
	for (auto &[name, override_value] : overrides) {
		percentage |= std::holds_alternative<d::GroupRollout>(override_value);
		def.overrides.emplace(name, override_value);
	}
	if (percentage)
		def.rollout_value = value;
	return def;
}
} // namespace
int main() {
	// Pinned values.
	CHECK(lifted(make(1, {}), "ns", "k", {}, {}) == 1);
	CHECK(lifted(make(-7, {}), "ns", "k", "u", {}) == -7);
	CHECK(lifted(make(1, 0), "ns", "k", {}, {}) == 1);            // no hash: the default
	CHECK(lifted(make(0, 0), "ns", "k", "u", {}) == 0);           // 0% ramps nobody
	CHECK(lifted(make(1, 0), "ns", "k", "u", {}) == 1);           // out of the bucket: the default
	CHECK(lifted(make(0, 100), "ns", "k", "u", {}) == 1);         // 100% ramps everybody
	CHECK(lifted(make(0, 100), "ns", "k", {}, {}) == 0);          // ...but only with a hash
	CHECK(lifted(make(25, 100, {}, 50), "ns", "k", "u", {}) == 50);
	CHECK(lifted(make(0, 100, {{"g", std::int64_t(1)}}), "ns", "k", {}, "g") == 1);   // forced wins with no hash
	CHECK(lifted(make(1, 100, {{"g", std::int64_t(0)}}), "ns", "k", "u", "g") == 0);
	CHECK(lifted(make(1, 100, {{"g", std::int64_t(0)}}), "ns", "k", "u", "other") == 1);
	CHECK(lifted(make(25, {}, {{"g", std::int64_t(INT64_MIN)}}), "ns", "k", "u", "g") == INT64_MIN);
	CHECK(lifted(make(1, {}, {{"g", d::GroupRollout{0}}}), "ns", "k", "u", "g") == 1);
	CHECK(lifted(make(0, {}, {{"g", d::GroupRollout{100}}}), "ns", "k", "u", "g") == 1);
	CHECK(lifted(make(1, {}, {{"g", d::GroupRollout{0}}}), "ns", "k", {}, "g") == 1);
	// A group percentage beats the knob-wide rollout: global 100 would serve the rollout value.
	CHECK(lifted(make(0, 100, {{"g", d::GroupRollout{0}}}), "ns", "k", "u", "g") == 0);
	// The bucket is stable_bucket("ns:k", hash), and the percentage is exclusive: bucket < pct.
	unsigned ramped = 0;
	for (unsigned i = 0; i != 400; ++i) {
		const auto hash = "user-" + std::to_string(i);
		const auto bucket = d::stable_bucket("ns:k", hash);
		for (std::uint8_t pct : {0, 1, 37, 50, 99, 100}) {
			CHECK(lifted(make(0, pct), "ns", "k", hash, {}) == (bucket < pct));
			CHECK(lifted(make(25, pct, {}, 50), "ns", "k", hash, {}) == (bucket < pct ? 50 : 25));
			CHECK(lifted(make(25, {}, {{"g", d::GroupRollout{pct}}}, -3), "ns", "k", hash, "g") ==
			      (bucket < pct ? -3 : 25));
		}
		ramped += bucket < 50;
	}
	CHECK(ramped > 100 && ramped < 300);
	// Oracle: detail::evaluate over KnobDef on the full small domain, bool and integer shapes.
	std::vector<std::optional<std::string>> hashes{std::nullopt}, groups{std::nullopt, "g", "other"};
	for (unsigned i = 0; i != 25; ++i)
		hashes.push_back("h" + std::to_string(i));
	std::vector<std::optional<std::uint8_t>> globals{std::nullopt, 0, 1, 50, 100};
	std::vector<std::vector<std::pair<std::string, d::GroupOverride>>> tables{
	    {}, {{"g", std::int64_t(1)}}, {{"g", std::int64_t(0)}}, {{"g", std::int64_t(-9)}},
	    {{"g", d::GroupRollout{0}}}, {{"g", d::GroupRollout{50}}}, {{"g", d::GroupRollout{100}}}};
	unsigned cases = 0;
	for (std::int64_t fallback : {std::int64_t(0), std::int64_t(1), std::int64_t(25), INT64_MIN})
		for (std::int64_t value : {std::int64_t(1), std::int64_t(50), INT64_MAX})
			for (auto global : globals)
				for (auto &table : tables)
					for (auto &group : groups)
						for (auto &hash : hashes) {
							const auto def = make(fallback, global, table, value);
							std::optional<std::string_view> h, g;
							if (hash)
								h = *hash;
							if (group)
								g = *group;
							CHECK(lifted(def, "ns", "k", h, g) == d::detail::evaluate(def, "ns:k", h, g));
							++cases;
						}
	std::cout << cases << " cases\n";
	return 0;
}
