#pragma once
// Test helper (reader design D9, ruling R2): writes real generations into a store directory through
// the agent's own paged publication. Never linked into a reader; see docs/mechanisms/generation-writer.md.
#include "detent/model.hh"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace detent::testing {
class GenerationWriter {
  public:
	// Creates the store directory (mode 0700) when it does not exist and opens it for publication.
	explicit GenerationWriter(std::filesystem::path store);
	~GenerationWriter();
	GenerationWriter(const GenerationWriter &) = delete;
	GenerationWriter &operator=(const GenerationWriter &) = delete;
	// Publishes the next revision holding exactly these knobs (keys are "namespace:name") and returns
	// that revision. Throws on a malformed knob set or a refused publication.
	std::uint64_t publish(const Snapshot &knobs);
	// Same, from the defaults-file JSON the authority parses ({"schema_version":1,"knobs":{...}}).
	std::uint64_t publish_json(std::string_view json);
	// Repairs the head: publishes these knobs again at the head's own revision as a fresh generation that
	// shares no pack with it and is recovered without reading it. The knobs are the head's content, which the
	// caller holds; nothing checks that they equal it. Returns the revision, unchanged.
	std::uint64_t repair_json(std::string_view json);
	// Runs the collector: removes the generations and packs nothing reaches (no reader holds a lease on them).
	void collect();
	// Maintenance publications on the current head, through the agent's own delta and compaction builders.
	// `publish_delta` applies one transaction to one namespace: each upsert sets an int knob and each name in
	// `removals` deletes one (all names are "namespace:name"); `scope` is every namespace of the store, sorted.
	// It returns the new revision.
	std::uint64_t publish_delta(const std::vector<std::string> &scope, std::string_view name_space,
								const std::map<std::string, std::int64_t> &upserts,
								const std::vector<std::string> &removals);
	// Rewrites up to `packs` packs of the head in place of themselves (the revision does not change).
	void compact(std::size_t packs);
	std::uint64_t revision() const noexcept;
	const std::filesystem::path &store() const noexcept;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::testing
