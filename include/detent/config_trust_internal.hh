#pragma once

#include <cerrno>

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
	if (!acl) return false;
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

} // namespace detent::internal
