#pragma once
#include "detent/model.hh"
#include "detent_local/view.hh"
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
// Borrowed flat rows retain validated image bytes independently of disk metadata.
class FlatStateRows {
  public:
	explicit FlatStateRows(ReadView);
	const Header &header() const;
	std::uint32_t namespace_count() const noexcept;
	std::uint32_t record_count() const noexcept;
	std::uint64_t override_count() const noexcept;
	SourceNamespace name_space(std::uint32_t) const;
	SourceRecord record(std::uint32_t) const;
	SourceOverride override_value(std::uint64_t) const;

  private:
	ReadView view_;
};
} // namespace detent::local::detail
