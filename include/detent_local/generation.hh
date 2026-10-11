#pragma once
#include "view.hh"
#include <array>
#include <filesystem>

namespace detent::local {
namespace detail {
class LeasedPagedRoot;
class PagedPlanBuilder;
} // namespace detail
using GenerationId = std::array<char, 16>;
struct Descriptor {
	GenerationId generation{};
	std::uint64_t length = 0;
	std::array<char, 32> checksum{};
	distribution::Cursor cursor;
	distribution::Profile profile;
	bool operator==(const Descriptor &) const = default;
};
inline constexpr std::size_t descriptor_bytes = 176;
std::array<char, descriptor_bytes> encode_descriptor(const Descriptor &descriptor);
Descriptor decode_descriptor(std::span<const char> bytes);
std::string generation_name(const GenerationId &generation);
std::string_view generation_name(const GenerationId &generation, std::span<char> destination);
struct DirectoryIdentity {
	std::uint64_t device = 0, inode = 0;
	bool operator==(const DirectoryIdentity &) const = default;
};
// Metadata only: this is not evidence of image validation or a publication base.
struct GenerationSelection {
	Descriptor descriptor;
	DirectoryIdentity directory_identity;
};
GenerationSelection read_generation_selection(const std::filesystem::path &directory);
class RecoveredGeneration {
  public:
	const ReadView &view() const noexcept { return view_; }
	const Descriptor &descriptor() const noexcept { return descriptor_; }
	DirectoryIdentity directory_identity() const noexcept { return directory_identity_; }

  private:
	RecoveredGeneration(ReadView view, Descriptor descriptor, std::shared_ptr<const void> directory,
						DirectoryIdentity identity,
						distribution::RetirementHandle<detail::LeasedPagedRoot> paged = {})
		: view_(std::move(view)), descriptor_(std::move(descriptor)), directory_(std::move(directory)),
		  directory_identity_(identity), paged_(std::move(paged)) {}
	ReadView view_;
	Descriptor descriptor_;
	std::shared_ptr<const void> directory_;
	DirectoryIdentity directory_identity_;
	distribution::RetirementHandle<detail::LeasedPagedRoot> paged_;
	friend class detail::PagedPlanBuilder;
	friend RecoveredGeneration recover_generation(const std::filesystem::path &, ViewLimits,
												  std::optional<distribution::Cursor>);
};
RecoveredGeneration recover_generation(const std::filesystem::path &directory, ViewLimits limits = {},
									   std::optional<distribution::Cursor> expected = {});
// Called on startup or a refresh worker; never on the hot lookup path.
// The directory must be trusted and owned by the publishing service account.
// All ReadViews retain a shared kernel lease until their mapped bytes retire.
ReadView open_generation(const std::filesystem::path &directory, ViewLimits limits = {},
						 std::optional<distribution::Cursor> expected = {});
} // namespace detent::local
