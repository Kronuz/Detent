#pragma once
#include "config.hh"
#include "detent_local/view.hh"
#include <functional>
#include <string_view>

namespace detent_agent {
inline constexpr std::string_view reader_fields[]{
	"records", "namespaces", "overrides", "image_bytes", "string_bytes",
	"pages", "segments", "heap_bytes", "mapping_bytes", "owners"};
bool is_reader_option(std::string_view flag);
class ReaderCapacity {
  public:
	using Setting = std::function<std::optional<std::string>(const std::string &, const char *)>;
	explicit ReaderCapacity(const Setting &setting);
	ReaderCapacity(const ReaderCapacity &) = delete;
	ReaderCapacity &operator=(const ReaderCapacity &) = delete;
	ReaderCapacity(ReaderCapacity &&) = delete;
	ReaderCapacity &operator=(ReaderCapacity &&) = delete;
	const detent::local::ViewLimits &limits() const noexcept { return limits_; }

  private:
	std::shared_ptr<detent::distribution::StorageLedger> baseline_, heap_;
	std::unique_ptr<detent::distribution::RetirementRuntime> runtime_;
	std::optional<detent::distribution::RetirementDomain> domain_;
	detent::local::ViewLimits limits_;
};
} // namespace detent_agent
