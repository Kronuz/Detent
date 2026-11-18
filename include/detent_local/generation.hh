#pragma once
#include "view.hh"
#include <array>
#include <filesystem>
#include <stdexcept>

namespace detent::local {
namespace detail {
class LeasedPagedRoot;
class PagedDependencyCertificate;
struct StoreCore;
class PagedPlanBuilder;
class PagedDeltaPlanBuilder;
class PagedCompactionBuilder;
class PagedCursor;
class WarmGraphValidator;
class PagedSnapshotSource;
class RecoveryEngine;
class StateCursor;
class PreparedReader;
struct RecoveredTesting; // test-only probe of a recovered generation
struct ScrubSource; // the agent's scrub reads the recovered registry
} // namespace detail
using GenerationId = std::array<char, 16>;
struct Descriptor {
	GenerationId generation{};
	std::uint64_t length = 0;
	std::array<char, 32> checksum{};
	distribution::Cursor cursor;
	distribution::Profile profile;
	std::uint32_t ordinal = 0; // D8.2's build ordinal: r + 1 for a rebuild at r's unchanged head
	bool operator==(const Descriptor &) const = default;
};
inline constexpr std::size_t prepared_metadata_bytes = 192;
// A nonzero generation over a valid cursor and profile; throws otherwise.
void validate_descriptor(const Descriptor &descriptor);
// One generation: equal but for the build ordinal, which its derived id binds (D8.1) and which a
// generation read by name (its lease and root) does not hold.
bool same_generation(const Descriptor &, const Descriptor &) noexcept;
std::string generation_name(const GenerationId &generation);
std::string_view generation_name(const GenerationId &generation, std::span<char> destination);
struct DirectoryIdentity {
	std::uint64_t device = 0, inode = 0;
	bool operator==(const DirectoryIdentity &) const = default;
};
// Metadata only: this is not evidence of image validation or a publication base.
class GenerationUnavailable : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
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
	friend struct detail::StoreCore;
	friend class detail::PagedDependencyCertificate;
	friend class detail::PagedPlanBuilder;
	friend class detail::PagedDeltaPlanBuilder;
	friend class detail::PagedCompactionBuilder;
	friend class detail::PagedCursor;
	friend class detail::WarmGraphValidator;
	friend class detail::PagedSnapshotSource;
	friend class detail::RecoveryEngine;
	friend class detail::StateCursor;
	friend class GenerationReader;
	friend class detail::PreparedReader;
	friend struct detail::RecoveredTesting;
	friend struct detail::ScrubSource;
	friend RecoveredGeneration recover_generation(const std::filesystem::path &, ViewLimits,
												  std::optional<distribution::Cursor>);
	friend RecoveredGeneration recover_generation(const std::filesystem::path &, ViewLimits,
												  std::optional<distribution::Cursor>,
												  const RecoveredGeneration *);
};
RecoveredGeneration recover_generation(const std::filesystem::path &directory, ViewLimits limits = {},
									   std::optional<distribution::Cursor> expected = {});
// A recovered prior root permits checked progress reuse; other heads recover cold.
RecoveredGeneration recover_generation(const std::filesystem::path &, ViewLimits,
									   std::optional<distribution::Cursor>, const RecoveredGeneration *prior);
// Called on startup or a refresh worker; never on the hot lookup path.
// The directory must be trusted and owned by the publishing service account.
// All ReadViews retain a shared kernel lease until their mapped bytes retire.
ReadView open_generation(const std::filesystem::path &directory, ViewLimits limits = {},
						 std::optional<distribution::Cursor> expected = {});
} // namespace detent::local
