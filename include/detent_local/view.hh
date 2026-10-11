#pragma once
#include "detent/errors.hh"
#include "detent/result.hh"
#include "detent_distribution/retirement.hh"
#include "layout.hh"
#include <memory>
#include <optional>

namespace detent::local {
namespace detail {
class PinnedImageSource;
class GraphValidator;
} // namespace detail
struct ValidationBudget {
	std::size_t bytes = 8192, rows = 16;
};
struct ViewLimits {
	std::uint64_t bytes = 256ull * 1024 * 1024, strings = 128ull * 1024 * 1024;
	std::uint32_t namespaces = 4096, records = 500000, overrides = 256;
	// Optional application-owned runtime domain. Its runner must outlive all
	// admitted views; closing the domain leaves existing views usable.
	distribution::RetirementDomain *retirement_domain = nullptr;
	// Mapped virtual bytes are counted separately from managed heap capacity.
	std::shared_ptr<distribution::StorageLedger> mapping_account;
};
class ImageValidator;
class ReadView {
  public:
	ReadView() = default;
	explicit operator bool() const noexcept { return static_cast<bool>(root_); }
	const distribution::Cursor &cursor() const;
	std::uint32_t size() const noexcept;
	std::uint32_t bool_count() const noexcept;
	std::uint32_t int_count() const noexcept;
	bool contains(std::string_view name_space, std::string_view key) const;
	// Absence is represented without constructing an allocating KnobError.
	std::optional<Result<bool, KnobError>> try_eval(std::string_view name_space, std::string_view key,
													std::optional<std::string_view> hash = {},
													std::optional<std::string_view> switch_value = {}) const;
	std::optional<Result<std::int64_t, KnobError>> try_get(std::string_view name_space,
														   std::string_view key) const;
	Result<bool, KnobError> eval(std::string_view name_space, std::string_view key,
								 std::optional<std::string_view> hash = {},
								 std::optional<std::string_view> switch_value = {}) const;
	Result<std::int64_t, KnobError> get(std::string_view name_space, std::string_view key) const;

  private:
	struct Root;
	explicit ReadView(distribution::RetirementHandle<Root> root) : root_(std::move(root)) {}
	distribution::RetirementHandle<Root> root_;
	friend class ImageValidator;
	friend class detail::PinnedImageSource;
	friend class detail::GraphValidator;
};
// The supplied lease retains the immutable byte region through all views.
// GenerationReader will supply a mapped image and its acquired kernel lease.
// No view can escape before every region and semantic row validates.
class ImageValidator {
  public:
	ImageValidator(std::shared_ptr<const void> lease, std::span<const char> bytes, ViewLimits limits = {},
				   std::optional<distribution::Cursor> expected = {});
	~ImageValidator();
	ImageValidator(ImageValidator &&) noexcept;
	ImageValidator(const ImageValidator &) = delete;
	void step(ValidationBudget &budget);
	bool done() const noexcept;
	ReadView take();

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local
