#pragma once
#include "generation.hh"

namespace detent::local {
namespace detail {
class DirectoryProvisioning;
}
// Startup-worker operation. Retain this descriptor through store construction
// and verify the resulting store identity before releasing it.
class ProvisionedDirectory {
  public:
	ProvisionedDirectory(const ProvisionedDirectory &) = delete;
	ProvisionedDirectory &operator=(const ProvisionedDirectory &) = delete;
	ProvisionedDirectory(ProvisionedDirectory &&) noexcept;
	~ProvisionedDirectory();
	DirectoryIdentity identity() const noexcept { return identity_; }
	void verify_name() const;

  private:
	friend class detail::DirectoryProvisioning;
	ProvisionedDirectory(int, std::filesystem::path, DirectoryIdentity);
	int descriptor_;
	std::filesystem::path path_;
	DirectoryIdentity identity_;
};
// Creates missing components without changing existing permissions. Directory
// namespace barriers precede handoff; the store's platform device flush still
// must succeed before readiness. Trusted existing ancestor links are followed,
// while newly created components and the final component reject symlinks.
ProvisionedDirectory provision_directory(const std::filesystem::path &, unsigned mode);
} // namespace detent::local
