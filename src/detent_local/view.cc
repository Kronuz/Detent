#include "detent_local/view.hh"
#include "evaluation.hh"
#include "detent/hashing.hh"
#include "detent/utf8.hh"
#include "detent/validation.hh"
#include "read_root.hh"
#include "resources.hh"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace detent::local {
namespace {
bool valid_lookup(std::string_view ns, std::string_view key) {
	return !ns.empty() && !key.empty() && ns.size() <= 198 && key.size() <= 198 &&
		   ns.size() + key.size() + 1 <= 200 &&
		   std::all_of(ns.begin(), ns.end(), detent::detail::is_name_char) &&
		   std::all_of(key.begin(), key.end(), detent::detail::is_name_char);
}
KnobError error(ErrorCategory category, std::string_view key) {
	return {category, std::string(key), "local image lookup failed"};
}
} // namespace
namespace resources {
namespace {
struct Defaults {
	std::shared_ptr<distribution::StorageLedger> baseline =
		std::make_shared<distribution::StorageLedger>(1024 * 1024);
	std::shared_ptr<distribution::StorageLedger> controls =
		std::make_shared<distribution::StorageLedger>(8 * 1024 * 1024);
	std::shared_ptr<distribution::StorageLedger> mappings =
		std::make_shared<distribution::StorageLedger>(512 * 1024 * 1024);
	distribution::RetirementRuntime runtime{baseline, 1};
	distribution::RetirementDomain domain{runtime.domain(controls, 16384)};
};
Defaults &defaults() {
	// Standalone readers share process-lifetime resources. Ordered teardown
	// uses an application-owned domain, not this default singleton.
	static auto *value = new Defaults;
	return *value;
}
} // namespace
distribution::RetirementDomain &domain(const ViewLimits &limits) {
	return limits.retirement_domain ? *limits.retirement_domain : defaults().domain;
}
std::shared_ptr<distribution::StorageLedger> mapping_account(const ViewLimits &limits) {
	return limits.mapping_account ? limits.mapping_account : defaults().mappings;
}
} // namespace resources
ViewLimits ViewLimits::prepared() {
    ViewLimits limits;
    limits.bytes = limits.strings = limits.pages = limits.segments = UINT64_MAX;
    limits.records = limits.namespaces = UINT32_MAX;
    return limits;
}
ReadView::Root::Root(std::shared_ptr<const void> owner, detail::PagedReadState state, std::uint64_t overrides,
					 std::uint64_t strings) {
	install_paged(std::move(owner), state, overrides, strings);
}
void ReadView::Root::install_paged(std::shared_ptr<const void> owner, detail::PagedReadState state,
								   std::uint64_t override_count, std::uint64_t string_bytes) {
	if (lease || paged || !owner)
		throw std::logic_error("paged read root installation unavailable");
	lease = std::move(owner);
	paged = state;
	overrides = override_count;
	strings = string_bytes;
}
std::optional<detail::BorrowedDefinition> ReadView::Root::definition(std::string_view ns,
																	 std::string_view key) const {
	return paged->definition(ns, key);
}
const distribution::NamespaceCursor &ReadView::cursor() const {
	if (!root_)
		throw std::logic_error("empty local read view");
	return root_->paged->root.cursor;
}
std::uint32_t ReadView::size() const noexcept { return root_ ? root_->paged->root.records : 0; }
bool ReadView::contains(std::string_view ns, std::string_view key) const {
	return root_ && valid_lookup(ns, key) && root_->definition(ns, key).has_value();
}
Result<std::int64_t, KnobError> ReadView::get(std::string_view ns, std::string_view key,
											  std::optional<std::string_view> hash,
											  std::optional<std::string_view> switch_value) const {
	if (!root_)
		return error(ErrorCategory::Uninitialized, key);
	auto result = try_get(ns, key, hash, switch_value);
	return result ? std::move(*result)
				  : Result<std::int64_t, KnobError>(error(ErrorCategory::MissingKnob, key));
}
std::optional<Result<std::int64_t, KnobError>> ReadView::try_get(std::string_view ns, std::string_view key,
																 std::optional<std::string_view> hash,
																 std::optional<std::string_view> switch_value) const {
	if (!valid_lookup(ns, key))
		return error(ErrorCategory::InvalidName, key);
	if (!root_)
		return {};
	try {
		const auto definition = root_->definition(ns, key);
		if (!definition)
			return {};
		return evaluate(*definition, ns, key, hash, switch_value);
	} catch (const std::bad_alloc &) {
		throw;
	} catch (const distribution::StoragePressure &) {
		throw;
	} catch (const std::exception &) {
		return error(ErrorCategory::SourceFailure, key);
	}
}
} // namespace detent::local
