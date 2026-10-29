#pragma once
#include "detent/config_trust_internal.hh"
#include "detent_local/generation.hh"
#include "generation_mapping.hh"
#include <array>
#include <cerrno>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <sys/stat.h>
#include <vector>

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
	std::array<std::optional<RecoveryFile>, 256> parents;
	std::size_t depth = 0;
	std::deque<std::filesystem::path> pending;
	auto prepend = [&](const std::filesystem::path &target) {
		std::vector<std::filesystem::path> parts;
		for (const auto &part : target.relative_path())
			parts.push_back(part);
		if (parts.size() + pending.size() > 256)
			throw std::runtime_error("publisher directory has too many components");
		for (auto it = parts.rbegin(); it != parts.rend(); ++it)
			pending.push_front(*it);
	};
	prepend(absolute);
	unsigned components = 0, links = 0;
	while (!pending.empty()) {
		if (++components > 256)
			throw std::runtime_error("publisher directory has too many components");
		const auto part = std::move(pending.front());
		pending.pop_front();
		if (part.empty() || part == ".")
			continue;
		if (part == "..") {
			if (depth) {
				std::swap(current.fd, parents[depth - 1]->fd);
				parents[--depth].reset();
			}
			continue;
		}
		struct stat entry{};
		if (::fstatat(current.fd, part.c_str(), &entry, AT_SYMLINK_NOFOLLOW) != 0) {
			if (errno == ENOENT)
				throw GenerationUnavailable("publisher directory is not available");
			throw std::runtime_error("cannot inspect publisher directory component");
		}
		if (S_ISLNK(entry.st_mode)) {
			struct stat parent{}, after{};
			if (++links > 40 || entry.st_uid != 0 || ::fstat(current.fd, &parent) != 0 || parent.st_uid != 0)
				throw std::runtime_error("publisher directory has an untrusted alias");
			std::array<char, 4097> target{};
			const auto count = ::readlinkat(current.fd, part.c_str(), target.data(), target.size());
			if (count <= 0 || std::size_t(count) >= target.size() ||
			    ::fstatat(current.fd, part.c_str(), &after, AT_SYMLINK_NOFOLLOW) != 0 ||
			    entry.st_dev != after.st_dev || entry.st_ino != after.st_ino ||
			    entry.st_uid != after.st_uid || entry.st_mode != after.st_mode)
				throw std::runtime_error("publisher directory alias changed");
			const std::filesystem::path resolved(std::string(target.data(), std::size_t(count)));
			if (resolved.is_absolute()) {
				while (depth) {
					std::swap(current.fd, parents[depth - 1]->fd);
					parents[--depth].reset();
				}
			}
			prepend(resolved);
			continue;
		}
		RecoveryFile next(
		    ::openat(current.fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
		if (next.fd < 0 && errno == ENOENT)
			throw GenerationUnavailable("publisher directory is not available");
		struct stat held{}, named{};
		if (next.fd < 0 || ::fstat(next.fd, &held) != 0 || !S_ISDIR(held.st_mode) ||
		    (held.st_uid != 0 && held.st_uid != owner) || (held.st_mode & 0022) ||
		    !internal::acceptable_acl(next.fd, internal::AclPolicy::DenialsAllowed) ||
		    ::fstatat(current.fd, part.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0 ||
		    held.st_dev != named.st_dev || held.st_ino != named.st_ino)
			throw std::runtime_error("publisher directory has untrusted ancestry");
		if (depth == parents.size())
			throw std::runtime_error("publisher directory has too many components");
		parents[depth].emplace(-1);
		std::swap(current.fd, parents[depth++]->fd);
		std::swap(current.fd, next.fd);
	}
	struct stat final_status{};
	if (::fstat(current.fd, &final_status) != 0 || final_status.st_uid != owner)
		throw std::runtime_error("publisher directory has an unexpected owner");
	return std::exchange(current.fd, -1);
}
} // namespace detent::local::detail
