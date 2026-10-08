#pragma once
#include "detent/model.hh"
#include "detent_local/generation.hh"

namespace detent::local::detail {
struct SourceNamespace {
	std::string_view name;
	std::uint64_t revision = 0;
};
struct SourceBoolean {
	bool default_value = false;
	std::optional<std::uint8_t> rollout_pct;
	std::uint64_t override_start = 0, override_count = 0;
};
struct SourceRecord {
	std::uint32_t namespace_index = 0;
	std::string_view key;
	std::uint64_t revision = 0;
	std::variant<SourceBoolean, IntDef> definition;
};
struct SourceOverride {
	std::string_view name;
	GroupOverride value;
};
// Build/export worker capability. Borrowed rows retain their mapping and shared
// generation lease through this source; no legacy Snapshot or codec is needed.
class PinnedImageSource {
  public:
	explicit PinnedImageSource(RecoveredGeneration generation);
	PinnedImageSource(const PinnedImageSource &) = default;
	PinnedImageSource &operator=(const PinnedImageSource &) = delete;
	const Descriptor &descriptor() const noexcept { return generation_.descriptor(); }
	DirectoryIdentity directory_identity() const noexcept { return generation_.directory_identity(); }
	std::uint32_t namespace_count() const noexcept;
	std::uint32_t record_count() const noexcept;
	std::uint64_t override_count() const noexcept;
	SourceNamespace name_space(std::uint32_t ordinal) const;
	SourceRecord record(std::uint32_t ordinal) const;
	SourceOverride override_value(std::uint64_t ordinal) const;

  private:
	RecoveredGeneration generation_;
};
} // namespace detent::local::detail
