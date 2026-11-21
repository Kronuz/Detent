#pragma once
#include "generation.hh"
#include <mutex>

namespace detent::local {
// Refresh belongs on the application's configuration worker. Acquiring a view
// only retains an installed retirement root; it performs no filesystem work.
class GenerationReader {
  public:
	GenerationReader(std::filesystem::path directory, ViewLimits limits = {},
	                 std::optional<distribution::NamespaceCursor> identity = {},
	                 std::optional<std::uint32_t> prepared_publisher = {}, std::string name = {});
	// A prepared publisher enables leased, checked-path attachment; otherwise
	// refresh fully validates. Exceptions preserve the previous view.
	// Returns false when the selected descriptor is already installed.
	bool refresh();
	ReadView read_view() const;
	std::optional<Descriptor> descriptor() const;

  private:
	std::filesystem::path directory_;
	ViewLimits limits_;
	std::optional<distribution::NamespaceCursor> identity_;
	std::optional<std::uint32_t> prepared_publisher_;
	// The table entry read (D8); empty: the table's only entry.
	std::string name_;
	std::optional<std::array<char, 32>> state_commitment_;
	std::mutex refresh_mutex_;
	mutable std::mutex installed_mutex_;
	ReadView installed_;
	std::optional<Descriptor> descriptor_;
	std::optional<DirectoryIdentity> directory_identity_;
	std::optional<RecoveredGeneration> recovered_;
};
} // namespace detent::local
