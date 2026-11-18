#pragma once
#include "cursor.hh"
#include "errors.hh"
#include "model.hh"
#include "overrides.hh"
#include "result.hh"
#include <chrono>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace detent {
namespace detail {
// The checked narrowing every get_as<T> uses: bool takes exactly 0 and 1, an integer type
// takes what std::in_range<T> admits.
template <class T> std::optional<T> convert(std::int64_t value) noexcept {
	if constexpr (std::is_same_v<T, bool>) {
		if (value == 0 || value == 1)
			return value == 1;
		return std::nullopt;
	} else {
		if (!std::in_range<T>(value))
			return std::nullopt;
		return static_cast<T>(value);
	}
}
} // namespace detail
// An immutable evaluator is published by its source on the refresh worker.
// Acquiring it must perform neither IO nor per-read allocation.
class EvaluationView {
  public:
	virtual ~EvaluationView() = default;
	virtual Result<std::int64_t, KnobError> get(std::string_view, std::optional<std::string_view>,
												std::optional<std::string_view>) const = 0;
	virtual std::optional<distribution::Cursor> cursor() const noexcept = 0;
};
class ClientReadView {
  public:
	ClientReadView() = default;
	ClientReadView(std::shared_ptr<const Snapshot> snapshot,
				   std::shared_ptr<const OverrideTable> overrides = {}) noexcept;
	explicit ClientReadView(std::shared_ptr<const EvaluationView> evaluator) noexcept;
	Result<std::int64_t, KnobError> get(std::string_view name, std::optional<std::string_view> hash = {},
										std::optional<std::string_view> group = {}) const;
	template <class T>
	Result<T, KnobError> get_as(std::string_view name, std::optional<std::string_view> hash = {},
								std::optional<std::string_view> group = {}) const {
		auto result = get(name, hash, group);
		if (!result)
			return result.error();
		if (auto value = detail::convert<T>(result.value()))
			return *value;
		return KnobError{ErrorCategory::ConversionFailed, std::string(name), "value does not fit target type"};
	}
	std::optional<distribution::Cursor> cursor() const noexcept;

  private:
	std::shared_ptr<const Snapshot> snapshot_;
	std::shared_ptr<const OverrideTable> overrides_;
	std::shared_ptr<const EvaluationView> evaluator_;
};
struct ReadSourceStatus {
	std::optional<distribution::Cursor> cursor;
	std::chrono::steady_clock::time_point last_success;
	std::size_t knobs = 0;
	bool connected = false;
};
class ReadSource {
  public:
	virtual ~ReadSource() = default;
	virtual ClientReadView read_view() const noexcept = 0;
	virtual ReadSourceStatus status() const noexcept = 0;
};
using ReadSourceFactory = std::function<std::shared_ptr<ReadSource>(std::shared_ptr<const Snapshot>,
																	std::shared_ptr<const OverrideTable>)>;
} // namespace detent
