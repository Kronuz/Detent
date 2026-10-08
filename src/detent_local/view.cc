#include "detent_local/view.hh"
#include "detent/hashing.hh"
#include "detent/utf8.hh"
#include "detent/validation.hh"
#include "pinned_source.hh"
#include "resources.hh"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace detent::local {
namespace {
void require(bool condition) {
	if (!condition)
		throw std::runtime_error("invalid local image row");
}
bool valid_lookup(std::string_view ns, std::string_view key) {
	return !ns.empty() && !key.empty() && ns.size() <= 198 && key.size() <= 198 &&
		   ns.size() + key.size() + 1 <= 200 && std::all_of(ns.begin(), ns.end(), detent::detail::is_name_char) &&
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
		std::make_shared<distribution::StorageLedger>(4 * 1024 * 1024);
	std::shared_ptr<distribution::StorageLedger> mappings =
		std::make_shared<distribution::StorageLedger>(512 * 1024 * 1024);
	distribution::RetirementRuntime runtime{baseline, 1};
	distribution::RetirementDomain domain{runtime.domain(controls, 4096)};
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
struct ReadView::Root : distribution::RetirementNode {
	std::shared_ptr<const void> lease;
	std::span<const char> bytes;
	Header header;
	Root(std::shared_ptr<const void> owner, std::span<const char> region, Header metadata)
		: lease(std::move(owner)), bytes(region), header(metadata) {}
	std::uint64_t number(std::uint64_t offset, unsigned width) const {
		return read_integer(bytes, offset, width);
	}
	std::string_view text(std::uint64_t row) const {
		auto offset = number(row, 8), size = number(row + 8, 4);
		require(offset <= header.layout.strings && size <= header.layout.strings - offset);
		return {bytes.data() + header.layout.string_offset + offset, static_cast<std::size_t>(size)};
	}
	std::optional<std::uint64_t> find(std::string_view ns, std::string_view key) const {
		const auto &l = header.layout;
		std::uint32_t low = 0, high = l.namespaces;
		while (low < high) {
			auto middle = low + (high - low) / 2;
			if (text(l.namespace_offset + middle * namespace_bytes) < ns)
				low = middle + 1;
			else
				high = middle;
		}
		if (low == l.namespaces || text(l.namespace_offset + low * namespace_bytes) != ns)
			return {};
		auto ns_index = low;
		low = 0;
		high = l.records;
		while (low < high) {
			auto middle = low + (high - low) / 2;
			auto row = l.record_offset + middle * record_bytes;
			auto index = number(row + 12, 4);
			auto name = text(row);
			auto local = name.substr(name.find(':') + 1);
			if (index < ns_index || (index == ns_index && local < key))
				low = middle + 1;
			else
				high = middle;
		}
		if (low == l.records)
			return {};
		auto row = l.record_offset + low * record_bytes;
		auto name = text(row);
		if (number(row + 12, 4) != ns_index || name.substr(name.find(':') + 1) != key)
			return {};
		return row;
	}
};
detail::PinnedImageSource::PinnedImageSource(RecoveredGeneration generation)
	: generation_(std::move(generation)) {
	if (!generation_.view())
		throw std::invalid_argument("pinned image source has no validated generation");
}
std::uint32_t detail::PinnedImageSource::namespace_count() const noexcept {
	return generation_.view().root_->header.layout.namespaces;
}
std::uint32_t detail::PinnedImageSource::record_count() const noexcept {
	return generation_.view().root_->header.layout.records;
}
std::uint64_t detail::PinnedImageSource::override_count() const noexcept {
	return generation_.view().root_->header.layout.overrides;
}
detail::SourceNamespace detail::PinnedImageSource::name_space(std::uint32_t ordinal) const {
	if (ordinal >= namespace_count())
		throw std::out_of_range("pinned namespace ordinal");
	const auto &root = *generation_.view().root_;
	const auto row = root.header.layout.namespace_offset + std::uint64_t(ordinal) * namespace_bytes;
	return {root.text(row), root.number(row + 16, 8)};
}
detail::SourceRecord detail::PinnedImageSource::record(std::uint32_t ordinal) const {
	if (ordinal >= record_count())
		throw std::out_of_range("pinned record ordinal");
	const auto &root = *generation_.view().root_;
	const auto row = root.header.layout.record_offset + std::uint64_t(ordinal) * record_bytes;
	SourceRecord result{
		static_cast<std::uint32_t>(root.number(row + 12, 4)), root.text(row), root.number(row + 16, 8), {}};
	if (root.number(row + 44, 1))
		result.definition = IntDef{std::bit_cast<std::int64_t>(root.number(row + 24, 8))};
	else {
		SourceBoolean boolean;
		boolean.default_value = root.number(row + 24, 8) != 0;
		if (root.number(row + 45, 1))
			boolean.rollout_pct = static_cast<std::uint8_t>(root.number(row + 46, 1));
		boolean.override_start = root.number(row + 32, 8);
		boolean.override_count = root.number(row + 40, 4);
		result.definition = boolean;
	}
	return result;
}
detail::SourceOverride detail::PinnedImageSource::override_value(std::uint64_t ordinal) const {
	if (ordinal >= override_count())
		throw std::out_of_range("pinned override ordinal");
	const auto &root = *generation_.view().root_;
	const auto row = root.header.layout.override_offset + ordinal * override_bytes;
	const auto value = static_cast<std::uint8_t>(root.number(row + 13, 1));
	return {root.text(row),
			root.number(row + 12, 1) ? GroupOverride(GroupRollout{value}) : GroupOverride(bool(value))};
}
struct ImageValidator::Impl {
	distribution::RetirementHandle<ReadView::Root> root;
	ViewLimits limits;
	std::uint64_t ns = 0, record = 0, override_index = 0, strings = 0, remaining_charge = 0;
	std::uint64_t active_override = 0, active_count = 0;
	std::string_view previous_ns, previous_key, previous_group;
	std::uint64_t previous_index = 0;
	bool failed = false, complete = false;
	void string_row(std::uint64_t row, std::string_view value) {
		require(root->number(row, 8) == strings);
		strings += value.size();
	}
	std::uint64_t row_cost() const {
		const auto &l = root->header.layout;
		auto cost = [&](std::uint64_t row, std::uint64_t metadata, std::uint64_t maximum) {
			auto name = root->text(row);
			require(name.size() <= maximum);
			return metadata + name.size();
		};
		if (ns < l.namespaces)
			return cost(l.namespace_offset + ns * namespace_bytes, namespace_bytes, 198);
		if (active_count)
			return cost(l.override_offset + override_index * override_bytes, override_bytes, 128);
		if (record < l.records)
			return cost(l.record_offset + record * record_bytes, record_bytes, 200);
		return 0;
	}
	void row() {
		const auto &l = root->header.layout;
		if (ns < l.namespaces) {
			auto r = l.namespace_offset + ns * namespace_bytes;
			auto name = root->text(r);
			require(!name.empty() && name.size() <= 198 &&
					std::all_of(name.begin(), name.end(), detent::detail::is_name_char));
			require(ns == 0 || previous_ns < name);
			require(root->number(r + 12, 4) == 0 && root->number(r + 16, 8) <= root->header.cursor.revision);
			string_row(r, name);
			previous_ns = name;
			++ns;
			return;
		}
		if (active_count) {
			auto r = l.override_offset + override_index * override_bytes;
			auto name = root->text(r);
			require(is_valid_group_key(name) && valid_utf8(name, false) &&
					(active_override == 0 || previous_group < name));
			auto tag = root->number(r + 12, 1), value = root->number(r + 13, 1);
			require(tag <= 1 && value <= (tag ? 100 : 1) && root->number(r + 14, 2) == 0 &&
					root->number(r + 16, 8) == 0);
			string_row(r, name);
			previous_group = name;
			++override_index;
			++active_override;
			--active_count;
			return;
		}
		if (record < l.records) {
			auto r = l.record_offset + record * record_bytes;
			auto name = root->text(r);
			auto index = root->number(r + 12, 4);
			require(index < l.namespaces && is_valid_knob_name(name));
			require(name.substr(0, name.find(':')) ==
					root->text(l.namespace_offset + index * namespace_bytes));
			require(record == 0 || index > previous_index ||
					(index == previous_index && previous_key < name));
			auto rev = root->number(r + 16, 8);
			require(rev > 0 && rev <= root->number(l.namespace_offset + index * namespace_bytes + 16, 8));
			auto type = root->number(r + 44, 1), has_pct = root->number(r + 45, 1),
				 pct = root->number(r + 46, 1), count = root->number(r + 40, 4);
			require(type <= 1 && has_pct <= 1 && pct <= 100 && (has_pct || pct == 0) &&
					root->number(r + 47, 1) == 0);
			require(root->number(r + 32, 8) == override_index && count <= limits.overrides &&
					count <= l.overrides - override_index);
			require(type == 0 ? root->number(r + 24, 8) <= 1 : count == 0 && has_pct == 0);
			string_row(r, name);
			previous_index = index;
			previous_key = name;
			active_count = count;
			active_override = 0;
			previous_group = {};
			++record;
			return;
		}
		require(strings == l.strings && override_index == l.overrides);
		complete = true;
	}
};
ImageValidator::ImageValidator(std::shared_ptr<const void> lease, std::span<const char> bytes,
							   ViewLimits limits, std::optional<distribution::Cursor> expected)
	: impl_(distribution::make_storage_owner<Impl>(resources::domain(limits).storage_account(), std::malloc,
												   std::free)) {
	require(static_cast<bool>(lease));
	auto header = decode_header(bytes, limits.bytes);
	const auto &l = header.layout;
	require(l.length == bytes.size() && l.namespaces <= limits.namespaces && l.records <= limits.records &&
			l.strings <= limits.strings && l.overrides <= std::uint64_t(l.records) * limits.overrides);
	if (expected)
		require(header.cursor.same_identity(*expected) && header.cursor.revision >= expected->revision);
	impl_->limits = limits;
	auto &domain = resources::domain(limits);
	impl_->root = domain.make<ReadView::Root>(std::move(lease), bytes, header);
}
ImageValidator::~ImageValidator() = default;
ImageValidator::ImageValidator(ImageValidator &&) noexcept = default;
void ImageValidator::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed)
		throw std::logic_error("inactive local image validator");
	try {
		while (!impl_->complete && budget.rows) {
			if (!impl_->remaining_charge)
				impl_->remaining_charge = impl_->row_cost();
			auto charged = std::min<std::uint64_t>(budget.bytes, impl_->remaining_charge);
			budget.bytes -= charged;
			impl_->remaining_charge -= charged;
			if (impl_->remaining_charge)
				return;
			--budget.rows;
			impl_->row();
		}
	} catch (...) {
		impl_->failed = true;
		throw;
	}
}
bool ImageValidator::done() const noexcept { return impl_ && impl_->complete && !impl_->failed; }
ReadView ImageValidator::take() {
	if (!done())
		throw std::logic_error("local image validation incomplete");
	auto root = std::move(impl_->root);
	impl_.reset();
	return ReadView(std::move(root));
}
const distribution::Cursor &ReadView::cursor() const {
	if (!root_)
		throw std::logic_error("empty local read view");
	return root_->header.cursor;
}
std::uint32_t ReadView::size() const noexcept { return root_ ? root_->header.layout.records : 0; }
Result<std::int64_t, KnobError> ReadView::get(std::string_view ns, std::string_view key) const {
	if (!root_)
		return error(ErrorCategory::Uninitialized, key);
	if (!valid_lookup(ns, key))
		return error(ErrorCategory::InvalidName, key);
	auto row = root_->find(ns, key);
	if (!row)
		return error(ErrorCategory::MissingKnob, key);
	if (root_->number(*row + 44, 1) != 1)
		return error(ErrorCategory::WrongType, key);
	return std::bit_cast<std::int64_t>(root_->number(*row + 24, 8));
}
Result<bool, KnobError> ReadView::eval(std::string_view ns, std::string_view key,
									   std::optional<std::string_view> hash,
									   std::optional<std::string_view> switch_value) const {
	if (!root_)
		return error(ErrorCategory::Uninitialized, key);
	if (!valid_lookup(ns, key))
		return error(ErrorCategory::InvalidName, key);
	auto row = root_->find(ns, key);
	if (!row)
		return error(ErrorCategory::MissingKnob, key);
	if (root_->number(*row + 44, 1) != 0)
		return error(ErrorCategory::WrongType, key);
	auto fallback = root_->number(*row + 24, 8) != 0;
	auto ramp = [&](std::uint64_t pct) {
		return hash ? stable_bucket(root_->text(*row), *hash) < pct : fallback;
	};
	if (switch_value) {
		auto begin = root_->number(*row + 32, 8), count = root_->number(*row + 40, 4);
		std::uint64_t low = 0, high = count;
		while (low < high) {
			auto mid = low + (high - low) / 2;
			if (root_->text(root_->header.layout.override_offset + (begin + mid) * override_bytes) <
				*switch_value)
				low = mid + 1;
			else
				high = mid;
		}
		if (low < count) {
			auto r = root_->header.layout.override_offset + (begin + low) * override_bytes;
			if (root_->text(r) == *switch_value)
				return root_->number(r + 12, 1) ? ramp(root_->number(r + 13, 1))
												: root_->number(r + 13, 1) != 0;
		}
	}
	return root_->number(*row + 45, 1) ? ramp(root_->number(*row + 46, 1)) : fallback;
}
} // namespace detent::local
