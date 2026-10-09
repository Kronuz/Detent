#include "directory_internal.hh"
#include <cerrno>
#include <fcntl.h>
#include <iterator>
#include <optional>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace detent::local {
namespace {
[[noreturn]] void failure(const char *operation) {
	throw std::system_error(errno, std::generic_category(), operation);
}
struct File {
	int descriptor = -1;
	~File() {
		if (descriptor >= 0)
			::close(descriptor);
	}
};
struct stat inspect(int descriptor) {
	struct stat result{};
	if (::fstat(descriptor, &result))
		failure("inspect provisioned directory");
	return result;
}
DirectoryIdentity identity_of(const struct stat &value) {
	return {std::uint64_t(value.st_dev), std::uint64_t(value.st_ino)};
}
bool readonly(int descriptor) {
	struct statvfs status{};
	if (::fstatvfs(descriptor, &status))
		failure("inspect directory filesystem");
	return status.f_flag & ST_RDONLY;
}
void synchronize(int descriptor) {
	int result;
	do {
		result = ::fsync(descriptor);
	} while (result < 0 && errno == EINTR);
	if (result < 0)
		failure("synchronize provisioned directory");
}
} // namespace
ProvisionedDirectory::ProvisionedDirectory(int descriptor, std::filesystem::path path,
										   DirectoryIdentity identity)
	: descriptor_(descriptor), path_(std::move(path)), identity_(identity) {}
ProvisionedDirectory::ProvisionedDirectory(ProvisionedDirectory &&other) noexcept
	: descriptor_(std::exchange(other.descriptor_, -1)), path_(std::move(other.path_)),
	  identity_(other.identity_) {}
ProvisionedDirectory::~ProvisionedDirectory() {
	if (descriptor_ >= 0)
		::close(descriptor_);
}
void ProvisionedDirectory::verify_name() const {
	File named{::open(path_.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
	if (named.descriptor < 0)
		failure("reopen provisioned directory");
	if (identity_of(inspect(named.descriptor)) != identity_ || identity_of(inspect(descriptor_)) != identity_)
		throw std::runtime_error("provisioned directory was replaced");
}
ProvisionedDirectory detail::DirectoryProvisioning::run(const std::filesystem::path &input, unsigned mode,
														Sync sync) {
	if (input.empty() || input.native().size() > 4096 || !sync || (mode & ~0777u) || (mode & 0022) ||
		(mode & 0700) != 0700)
		throw std::invalid_argument("invalid directory provisioning options");
	// Allocate the eventual handoff path before namespace effects.
	auto path = input;
	while (path.filename().empty() && path.has_relative_path())
		path = path.parent_path();
	File parent{::open(path.is_absolute() ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
	if (parent.descriptor < 0)
		failure("open provisioning ancestor");
	std::optional<dev_t> created_device;
#ifdef __APPLE__
	std::optional<dev_t> writable_device;
#endif
	for (auto iterator = path.begin(); iterator != path.end(); ++iterator) {
		if (*iterator == path.root_directory() || iterator->empty())
			continue;
		const auto component = iterator->native();
		const bool final = std::next(iterator) == path.end();
		const bool created = ::mkdirat(parent.descriptor, component.c_str(), mode) == 0;
		if (!created && errno != EEXIST)
			failure("create managed directory component");
		File child{::openat(parent.descriptor, component.c_str(),
							O_RDONLY | O_DIRECTORY | O_CLOEXEC | ((created || final) ? O_NOFOLLOW : 0))};
		if (child.descriptor < 0)
			failure("open managed directory component");
		const auto before = inspect(parent.descriptor), current = inspect(child.descriptor);
		const bool child_readonly = readonly(child.descriptor), parent_readonly = readonly(parent.descriptor);
#ifdef __APPLE__
		// Also cover retries whose names survived an earlier incomplete attempt.
		// One final owner-file fullsync must cover every writable namespace edge.
		for (const auto &edge :
			 {std::pair{before.st_dev, parent_readonly}, std::pair{current.st_dev, child_readonly}}) {
			if (!edge.second) {
				if (writable_device && *writable_device != edge.first)
					throw std::runtime_error("macOS provisioning requires one writable filesystem");
				writable_device = edge.first;
			}
		}
#endif
		if (created) {
			if (before.st_dev != current.st_dev || (created_device && *created_device != current.st_dev))
				throw std::runtime_error("created directory crosses provisioning filesystem");
			created_device = current.st_dev;
			if (::fchmod(child.descriptor, mode))
				failure("set new managed directory permissions");
		}
		if (created_device && *created_device != current.st_dev)
			throw std::runtime_error("provisioning device flush cannot cover created ancestors");
		// Existing writable edges are synced too: a prior failed attempt may
		// have created this name without completing its parent barrier.
		if (created || !child_readonly)
			sync(child.descriptor);
		if (created || !parent_readonly)
			sync(parent.descriptor);
		struct stat named{};
		if (::fstatat(parent.descriptor, component.c_str(), &named,
					  (created || final) ? AT_SYMLINK_NOFOLLOW : 0))
			failure("verify provisioned name");
		if (!S_ISDIR(named.st_mode) || identity_of(named) != identity_of(current))
			throw std::runtime_error("managed directory component was replaced");
		::close(parent.descriptor);
		parent.descriptor = std::exchange(child.descriptor, -1);
	}
	const auto final_identity = identity_of(inspect(parent.descriptor));
	ProvisionedDirectory result(std::exchange(parent.descriptor, -1), std::move(path), final_identity);
	result.verify_name();
	return result;
}
ProvisionedDirectory provision_directory(const std::filesystem::path &path, unsigned mode) {
	return detail::DirectoryProvisioning::run(path, mode, synchronize);
}
} // namespace detent::local
