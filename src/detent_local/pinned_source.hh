#pragma once
#include "detent_local/generation.hh"
#include "flat_state_rows.hh"

namespace detent::local::detail {
// Build/export worker capability. Borrowed rows retain their mapping and shared
// generation lease through this source; no legacy Snapshot or codec is needed.
class PinnedImageSource {
  public:
	explicit PinnedImageSource(RecoveredGeneration generation);
	PinnedImageSource(const PinnedImageSource &) = default;
	PinnedImageSource &operator=(const PinnedImageSource &) = delete;
	const Descriptor &descriptor() const noexcept { return generation_.descriptor(); }
	DirectoryIdentity directory_identity() const noexcept { return generation_.directory_identity(); }
	std::uint32_t namespace_count() const noexcept { return rows_.namespace_count(); }
	std::uint32_t record_count() const noexcept { return rows_.record_count(); }
	std::uint64_t override_count() const noexcept { return rows_.override_count(); }
	SourceNamespace name_space(std::uint32_t ordinal) const { return rows_.name_space(ordinal); }
	SourceRecord record(std::uint32_t ordinal) const { return rows_.record(ordinal); }
	SourceOverride override_value(std::uint64_t ordinal) const { return rows_.override_value(ordinal); }

  private:
	RecoveredGeneration generation_;
	FlatStateRows rows_;
};
} // namespace detent::local::detail
