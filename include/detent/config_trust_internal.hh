#pragma once

#include <cerrno>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/acl.h>
#endif

namespace detent::internal {

// Return false when an ACL adds rules beyond the descriptor's mode bits.
inline bool mode_only_acl(int fd) {
#if defined(__APPLE__)
	acl_t acl = ::acl_get_fd_np(fd, ACL_TYPE_EXTENDED);
	if (!acl) return errno == ENOENT; // macOS reports no extended ACL this way.
	::acl_free(acl);
	return false;
#elif defined(__FreeBSD__)
	acl_t acl = ::acl_get_fd_np(fd, ACL_TYPE_NFS4);
	if (!acl && (errno == EINVAL || errno == EOPNOTSUPP))
		acl = ::acl_get_fd_np(fd, ACL_TYPE_ACCESS);
	if (!acl) return false;
	int trivial = 0;
	const bool inspected = ::acl_is_trivial_np(acl, &trivial) == 0;
	const bool freed = ::acl_free(acl) == 0;
	return inspected && trivial == 1 && freed;
#elif defined(__linux__)
	// Linux POSIX ACL write grants appear in the group-class mode mask.
	(void)fd;
	return true;
#else
	(void)fd;
	return false;
#endif
}

} // namespace detent::internal
