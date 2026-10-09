#pragma once
#include "detent_local/directory.hh"
namespace detent::local::detail {
class DirectoryProvisioning {
  public:
	using Sync = void (*)(int);
	static ProvisionedDirectory run(const std::filesystem::path &, unsigned, Sync);
};
} // namespace detent::local::detail
