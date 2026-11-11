// A1: every former bool knob, lifted to one int64 definition (default 0/1, rollout value 1),
// answers through Client::get exactly what main's bool evaluator answered, over the scenarios
// the evaluation tests cover. The one deliberate change (ruling B2, option 1) is enumerated
// separately: a default-true knob's hashed caller outside a percentage now gets the default.
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "detent/client.hh"
#include "detent/hashing.hh"

using namespace detent;

namespace {

void check(bool value, int line) {
	if (!value) {
		std::fprintf(stderr, "differential invariant failed at line %d\n", line);
		std::abort();
	}
}
#define CHECK(value) check(value, __LINE__)

// main's bool definition and evaluator, frozen (include/detent/detail/evaluation.hh at main).
struct OldPercentage {
	std::uint8_t rollout_pct = 0;
};
using OldOverride = std::variant<bool, OldPercentage>;
struct OldBool {
	bool default_value = false;
	flat_map<std::string, OldOverride, std::less<>> overrides;
	std::optional<std::uint8_t> rollout_pct;
};
bool old_evaluate(const OldBool &definition, std::string_view name, std::optional<std::string_view> hash,
				  std::optional<std::string_view> group) {
	if (group) {
		auto override = definition.overrides.find(*group);
		if (override != definition.overrides.end()) {
			if (const auto *forced = std::get_if<bool>(&override->second))
				return *forced;
			return hash ? stable_bucket(name, *hash) < std::get<OldPercentage>(override->second).rollout_pct
						: definition.default_value;
		}
	}
	return definition.rollout_pct && hash ? stable_bucket(name, *hash) < *definition.rollout_pct
										  : definition.default_value;
}

// A bool is default 0/1 with rollout value 1, present exactly when a percentage is.
KnobDef lift(const OldBool &old) {
	KnobDef result;
	result.default_value = old.default_value ? 1 : 0;
	result.rollout_pct = old.rollout_pct;
	bool percentage = old.rollout_pct.has_value();
	for (const auto &[key, value] : old.overrides) {
		if (const auto *forced = std::get_if<bool>(&value)) {
			result.overrides.emplace(key, std::int64_t(*forced ? 1 : 0));
		} else {
			result.overrides.emplace(key, GroupRollout{std::get<OldPercentage>(value).rollout_pct});
			percentage = true;
		}
	}
	if (percentage)
		result.rollout_value = 1;
	return result;
}

// The percentage that decides a cell: a matching group percentage, or the knob-wide rollout when
// no group override matches. A matching forced group decides without one.
std::optional<std::uint8_t> applied_percentage(const OldBool &old, std::optional<std::string_view> group) {
	if (group) {
		auto override = old.overrides.find(*group);
		if (override != old.overrides.end()) {
			if (const auto *pct = std::get_if<OldPercentage>(&override->second))
				return pct->rollout_pct;
			return std::nullopt;
		}
	}
	return old.rollout_pct;
}

// The changed set (B2 option 1): a default-true knob, a hash, and a bucket outside the
// percentage that applies. The old evaluator answered false there; the new one the default.
bool in_changed_set(const OldBool &old, std::string_view name, std::optional<std::string_view> group,
					std::optional<std::string_view> hash) {
	const auto pct = applied_percentage(old, group);
	return old.default_value && pct && hash && stable_bucket(name, *hash) >= *pct;
}

} // namespace

int main() {
	const std::string name = "ns:k";
	std::vector<std::optional<std::string>> hashes{std::nullopt};
	for (unsigned i = 0; i != 200; ++i)
		hashes.push_back("user-" + std::to_string(i));
	const std::vector<std::optional<std::string>> groups{std::nullopt, "g", "other"};
	const std::vector<std::optional<std::uint8_t>> rollouts{std::nullopt, 0, 1, 50, 99, 100};
	// Group tables: none, forced true/false, and percentages 0, 1, 50, 99 and 100 for "g".
	std::vector<std::optional<OldOverride>> tables{std::nullopt, OldOverride(true), OldOverride(false)};
	for (std::uint8_t pct : {0, 1, 50, 99, 100})
		tables.emplace_back(OldOverride(OldPercentage{pct}));

	unsigned long equal = 0, changed = 0;
	for (bool fallback : {false, true})
		for (const auto &rollout : rollouts)
			for (const auto &table : tables) {
				OldBool old;
				old.default_value = fallback;
				old.rollout_pct = rollout;
				if (table)
					old.overrides.emplace("g", *table);
				const auto lifted = lift(old);
				CHECK(definition_error(lifted) == nullptr);
				Snapshot snapshot;
				snapshot.knobs[name] = lifted;
				Client client(std::move(snapshot));
				for (const auto &group : groups)
					for (const auto &hash : hashes) {
						std::optional<std::string_view> g, h;
						if (group)
							g = *group;
						if (hash)
							h = *hash;
						const bool before = old_evaluate(old, name, h, g);
						const auto after = client.get(name, h, g);
						CHECK(after.has_value());
						if (in_changed_set(old, name, g, h)) {
							CHECK(!before && after.value() == 1);
							++changed;
						} else {
							CHECK(after.value() == (before ? 1 : 0));
							++equal;
						}
					}
			}
	std::printf("%lu equal cells, %lu changed cells (default-true, hashed, outside the percentage)\n", equal,
				changed);
	CHECK(equal > 0 && changed > 0);
	return 0;
}
