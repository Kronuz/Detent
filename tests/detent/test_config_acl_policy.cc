#include <sys/types.h>
#include <sys/acl.h>
#include <unistd.h>
#include <cerrno>
#include <initializer_list>
#include <stdexcept>

namespace {
int nfs_error = EOPNOTSUPP, access_error = EOPNOTSUPP;
long extended = 0, nfs4 = 0;
unsigned queries = 0;
acl_t failed_acl(int, acl_type_t type) {
	errno = type == ACL_TYPE_NFS4 ? nfs_error : access_error;
	return nullptr;
}
long capability(int, int name) {
	++queries;
	errno = 0;
	return name == _PC_ACL_EXTENDED ? extended : nfs4;
}
}
#define acl_get_fd_np failed_acl
#define fpathconf capability
#include "detent/config_trust_internal.hh"
#undef fpathconf
#undef acl_get_fd_np

void check(bool value) {
	if (!value)
		throw std::runtime_error("ACL failure policy invariant");
}
int main() {
	using namespace detent::internal;
	for (const auto policy : {AclPolicy::ModeOnly, AclPolicy::DenialsAllowed}) {
		for (const int error : {EOPNOTSUPP, EINVAL}) {
			nfs_error = error;
			access_error = EOPNOTSUPP;
			for (const long first : {-1L, 0L, 1L}) {
				for (const long second : {-1L, 0L, 1L}) {
					extended = first;
					nfs4 = second;
					check(acceptable_acl(123, policy) == (first == 0 && second == 0));
				}
			}
		}
		for (const int error : {EACCES, ENOMEM, EIO, EBADF, EINVAL}) {
			nfs_error = EOPNOTSUPP;
			access_error = error;
			extended = nfs4 = 0;
			queries = 0;
			check(!acceptable_acl(123, policy) && queries == 0);
		}
		for (const int error : {EACCES, ENOMEM, EIO, EBADF}) {
			nfs_error = error;
			access_error = EOPNOTSUPP;
			queries = 0;
			check(!acceptable_acl(123, policy) && queries == 0);
		}
	}
}
