#pragma once
// Test helper (reader design D9, ruling R2): writes real generations into a store directory through
// the agent's own paged publication. Never linked into a reader; see docs/mechanisms/generation-writer.md.
#include "detent/model.hh"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

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
	std::uint64_t revision() const noexcept;
	const std::filesystem::path &store() const noexcept;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::testing
