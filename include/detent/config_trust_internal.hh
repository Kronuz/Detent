#pragma once

#include <cerrno>
#include <sys/stat.h>

#if defined(__APPLE__)
#include <fcntl.h> // filesec_t
#endif
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/acl.h>
#include <unistd.h>
#endif

namespace detent::internal {

enum class AclPolicy { ModeOnly, DenialsAllowed };
#if defined(__APPLE__)
// Consumes `acl`. DenialsAllowed permits macOS deny-only ancestry without accepting extra grants.
inline bool accepted_acl(acl_t acl, AclPolicy policy) {
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
}
#endif
inline bool acceptable_acl(int fd, AclPolicy policy) {
#if defined(__APPLE__)
	acl_t acl = ::acl_get_fd_np(fd, ACL_TYPE_EXTENDED);
	if (!acl) return errno == ENOENT; // macOS reports no extended ACL this way.
	return accepted_acl(acl, policy);
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
// fstat, then `acl_ok = acceptable_acl(fd, policy)` when it succeeds. On macOS one fstatx_np
// returns both; acl_get_fd_np would issue it a second time. Returns whether the status was read.
inline bool status_and_acl(int fd, struct stat &info, AclPolicy policy, bool &acl_ok) {
	acl_ok = false;
#if defined(__APPLE__)
	filesec_t security = ::filesec_init();
	if (!security)
		return false; // allocation failure
	const bool stated = ::fstatx_np(fd, &info, security) == 0;
	if (stated) {
		acl_t acl = nullptr;
		if (::filesec_get_property(security, FILESEC_ACL, &acl) != 0)
			acl_ok = errno == ENOENT; // no extended ACL, as acl_get_fd_np reports it
		else
			acl_ok = accepted_acl(acl, policy);
	}
	::filesec_free(security);
	return stated;
#else
	if (::fstat(fd, &info) != 0)
		return false;
	acl_ok = acceptable_acl(fd, policy);
	return true;
#endif
}

} // namespace detent::internal
