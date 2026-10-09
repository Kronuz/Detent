#pragma once
#include "cursor.hh"
#include "errors.hh"
#include "model.hh"
#include "overrides.hh"
#include "result.hh"
#include <chrono>
#include <functional>
#include <memory>
#include <utility>

namespace detent {
// An immutable evaluator is published by its source on the refresh worker.
// Acquiring it must perform neither IO nor per-read allocation.
class EvaluationView {
  public:
	virtual ~EvaluationView() = default;
	virtual Result<bool, KnobError> eval(std::string_view, std::optional<std::string_view>,
										 std::optional<std::string_view>) const = 0;
	virtual Result<std::int64_t, KnobError> get(std::string_view) const = 0;
	virtual std::optional<distribution::Cursor> cursor() const noexcept = 0;
};
class ClientReadView {
  public:
	ClientReadView() = default;
	ClientReadView(std::shared_ptr<const Snapshot> snapshot,
				   std::shared_ptr<const OverrideTable> overrides = {}) noexcept;
	explicit ClientReadView(std::shared_ptr<const EvaluationView> evaluator) noexcept;
	Result<bool, KnobError> eval(std::string_view name, std::optional<std::string_view> hash = {},
								 std::optional<std::string_view> group = {}) const;
	Result<std::int64_t, KnobError> get(std::string_view name,
										std::optional<std::string_view> group = {}) const;
	template <class T>
	Result<T, KnobError> get_as(std::string_view name, std::optional<std::string_view> group = {}) const {
		auto result = get(name, group);
		if (!result)
			return result.error();
		if (!std::in_range<T>(result.value()))
			return KnobError{ErrorCategory::ConversionFailed, std::string(name),
							 "value does not fit target type"};
		return static_cast<T>(result.value());
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
	std::size_t bool_knobs = 0, int_knobs = 0;
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
