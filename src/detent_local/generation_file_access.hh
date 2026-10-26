#pragma once
#include "detent/config_trust_internal.hh"
#include "generation_mapping.hh"
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>

namespace detent::local::detail {
inline void check_generation_file(int fd, uid_t owner, std::uint64_t length) {
	struct stat info{};
	if (fd < 0 || ::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != owner ||
	    info.st_nlink != 1 || (info.st_mode & 0022) || info.st_size < 0 ||
	    std::uint64_t(info.st_size) != length)
		throw std::runtime_error("invalid local generation descriptor");
}
inline void read_generation_record(int fd, std::span<char> bytes) {
	for (std::size_t offset = 0; offset < bytes.size();) {
		const auto count = ::pread(fd, bytes.data() + offset, bytes.size() - offset, offset);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			throw std::runtime_error("invalid local generation descriptor");
		offset += static_cast<std::size_t>(count);
	}
}
inline int open_publisher_directory(const std::filesystem::path &path, uid_t owner) {
	if (path.empty() || path.native().find('\0') != std::string::npos)
		throw std::invalid_argument("invalid publisher directory path");
	const auto absolute = path.is_absolute() ? path : std::filesystem::current_path() / path;
	RecoveryFile current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
	struct stat anchor{};
	if (current.fd < 0 || ::fstat(current.fd, &anchor) != 0 || anchor.st_uid != 0 ||
	    !S_ISDIR(anchor.st_mode) || (anchor.st_mode & 0022) ||
	    !internal::acceptable_acl(current.fd, internal::AclPolicy::DenialsAllowed))
		throw std::runtime_error("cannot open publisher directory anchor");
	for (const auto &part : absolute.relative_path()) {
		if (part.empty() || part == ".")
			continue;
		if (part == "..")
			throw std::invalid_argument("publisher directory contains a parent traversal");
		RecoveryFile next(
		    ::openat(current.fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
		struct stat held{}, named{};
		if (next.fd < 0 || ::fstat(next.fd, &held) != 0 || !S_ISDIR(held.st_mode) ||
		    (held.st_uid != 0 && held.st_uid != owner) || (held.st_mode & 0022) ||
		    !internal::acceptable_acl(next.fd, internal::AclPolicy::DenialsAllowed) ||
		    ::fstatat(current.fd, part.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0 ||
		    held.st_dev != named.st_dev || held.st_ino != named.st_ino)
			throw std::runtime_error("publisher directory has untrusted ancestry");
		std::swap(current.fd, next.fd);
	}
	struct stat final_status {};
	if (::fstat(current.fd, &final_status) != 0 || final_status.st_uid != owner)
		throw std::runtime_error("publisher directory has an unexpected owner");
	return std::exchange(current.fd, -1);
}
} // namespace detent::local::detail
