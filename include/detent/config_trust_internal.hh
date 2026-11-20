#pragma once

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/acl.h>
#endif

namespace detent::internal {

enum class AclPolicy { ModeOnly, DenialsAllowed };
// DenialsAllowed permits macOS deny-only ancestry without accepting extra grants.
inline bool acceptable_acl(int fd, AclPolicy policy) {
#if defined(__APPLE__)
	acl_t acl = ::acl_get_fd_np(fd, ACL_TYPE_EXTENDED);
	if (!acl) return errno == ENOENT; // macOS reports no extended ACL this way.
	bool accepted = policy == AclPolicy::DenialsAllowed;
	if (accepted) {
		acl_entry_t entry;
		int cursor = ACL_FIRST_ENTRY;
		for (;;) {
			if (::acl_get_entry(acl, cursor, &entry) != 0) {
				accepted = errno == EINVAL;
				break;
			}
			acl_tag_t tag;
			if (::acl_get_tag_type(entry, &tag) != 0 || tag != ACL_EXTENDED_DENY) {
				accepted = false;
				break;
			}
			cursor = ACL_NEXT_ENTRY;
		}
	}
	return ::acl_free(acl) == 0 && accepted;
#elif defined(__FreeBSD__)
	(void)policy;
	acl_t acl = ::acl_get_fd_np(fd, ACL_TYPE_NFS4);
	if (!acl && (errno == EINVAL || errno == EOPNOTSUPP))
		acl = ::acl_get_fd_np(fd, ACL_TYPE_ACCESS);
	if (!acl)
		return errno == EOPNOTSUPP && ::fpathconf(fd, _PC_ACL_EXTENDED) == 0 &&
		       ::fpathconf(fd, _PC_ACL_NFS4) == 0;
	int trivial = 0;
	const bool inspected = ::acl_is_trivial_np(acl, &trivial) == 0;
	const bool freed = ::acl_free(acl) == 0;
	return inspected && trivial == 1 && freed;
#elif defined(__linux__)
	// Linux POSIX ACL write grants appear in the group-class mode mask.
	(void)fd;
	(void)policy;
	return true;
#else
	(void)fd;
	(void)policy;
	return false;
#endif
}
inline bool mode_only_acl(int fd) { return acceptable_acl(fd, AclPolicy::ModeOnly); }

// Startup only: a single-link regular file owned by root or the effective user, with no group write or
// other access and a mode-only ACL, bounded to 1 MiB. Credential files of the agent and the relay.
inline std::string read_private_configuration(const std::filesystem::path &path) {
	auto require = [](bool value, const char *message) {
		if (!value)
			throw std::invalid_argument(message);
	};
	struct File {
		int descriptor;
		~File() {
			if (descriptor >= 0)
				::close(descriptor);
		}
	} file{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
	struct stat info{};
	require(file.descriptor >= 0 && ::fstat(file.descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
				(info.st_uid == 0 || info.st_uid == ::geteuid()) && info.st_nlink == 1 &&
				!(info.st_mode & 0027) && mode_only_acl(file.descriptor) && info.st_size > 0 &&
				info.st_size <= 1024 * 1024,
			"private configuration ownership, permissions or size");
	std::string bytes(static_cast<std::size_t>(info.st_size), '\0');
	std::size_t offset = 0;
	while (offset != bytes.size()) {
		auto count = ::read(file.descriptor, bytes.data() + offset, bytes.size() - offset);
		if (count < 0 && errno == EINTR)
			continue;
		require(count > 0, "short private configuration");
		offset += count;
	}
	char extra;
	ssize_t remaining;
	do {
		remaining = ::read(file.descriptor, &extra, 1);
	} while (remaining < 0 && errno == EINTR);
	require(remaining == 0, "private configuration changed while reading");
	require(mode_only_acl(file.descriptor), "private configuration ACL changed while reading");
	return bytes;
}

} // namespace detent::internal
