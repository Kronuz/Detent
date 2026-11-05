#include "change_file.hh"
#include "detent/config_trust_internal.hh"
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace detent::local::detail {
namespace {
void (*publish_hook)(void *) noexcept = nullptr;
void *publish_context = nullptr;
// Words in a file that passes the shape checks, or 0.
std::size_t checked_words(const struct stat &info, uid_t owner) {
	if (!S_ISREG(info.st_mode) || info.st_uid != owner || info.st_nlink != 1 || (info.st_mode & 0022) ||
	    info.st_size < std::int64_t(change_bytes(0)) || info.st_size % 8)
		return 0;
	return std::size_t(info.st_size) / 8;
}
} // namespace
ChangeWriter::ChangeWriter(int directory, std::size_t slots, uid_t owner, unsigned mode) {
	if (directory < 0 || (mode & 0022) || (mode & ~0777u))
		throw std::invalid_argument("invalid change file arguments");
	bool created = false;
	do {
		fd_ = ::openat(directory, change_name, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
		if (fd_ < 0 && errno == ENOENT) {
			fd_ = ::openat(directory, change_name, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
			created = fd_ >= 0;
			if (fd_ < 0 && errno == EEXIST)
				continue;
		}
		break;
	} while (true);
	if (fd_ < 0)
		throw std::system_error(errno, std::generic_category(), "open change file");
	try {
		if (created && (::fchmod(fd_, mode) != 0 || ::ftruncate(fd_, off_t(change_bytes(0))) != 0))
			throw std::system_error(errno, std::generic_category(), "create change file");
		struct stat info{};
		if (::fstat(fd_, &info) != 0)
			throw std::system_error(errno, std::generic_category(), "inspect change file");
		const auto words = checked_words(info, owner);
		if (!words || !internal::mode_only_acl(fd_))
			throw ChangeRefused("unsafe change file");
		map(words);
		reserve(slots);
	} catch (...) {
		if (mapped_)
			::munmap(mapped_, words_ * 8);
		::close(fd_);
		throw;
	}
}
ChangeWriter::~ChangeWriter() {
	::munmap(mapped_, words_ * 8);
	::close(fd_);
}
void ChangeWriter::map(std::size_t words) {
	auto address = ::mmap(nullptr, words * 8, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
	if (address == MAP_FAILED)
		throw std::system_error(errno, std::generic_category(), "map change file");
	if (mapped_)
		::munmap(mapped_, words_ * 8);
	mapped_ = static_cast<ChangeWord *>(address);
	words_ = words;
}
void ChangeWriter::reserve(std::size_t slots) {
	if (slots + 1 <= words_)
		return;
	if (slots > (std::size_t(1) << 32))
		throw std::length_error("change file slot out of range");
	// Extension zero-fills; the agent never truncates, so this only grows.
	if (::ftruncate(fd_, off_t(change_bytes(slots))) != 0)
		throw std::system_error(errno, std::generic_category(), "grow change file");
	map(slots + 1);
}
void ChangeTesting::install(void (*hook)(void *) noexcept, void *context) noexcept {
	publish_context = context;
	publish_hook = hook;
}
void ChangeWriter::publish(std::span<const std::uint32_t> changed) noexcept {
	if (publish_hook)
		publish_hook(publish_context);
	for (const auto slot : changed)
		mapped_[std::size_t(slot) + 1].fetch_add(1, std::memory_order_release);
	mapped_[0].fetch_add(1, std::memory_order_release);
}
void ChangeWriter::bump_all() noexcept {
	for (std::size_t index = 1; index < words_; ++index)
		mapped_[index].fetch_add(1, std::memory_order_release);
	mapped_[0].fetch_add(1, std::memory_order_release);
}
std::uint64_t ChangeWriter::word(std::size_t index) const noexcept {
	return index < words_ ? mapped_[index].load(std::memory_order_acquire) : 0;
}

ChangeMapping::Open ChangeMapping::open(int directory, uid_t publisher, ChangeMapping &out) {
	const int fd = ::openat(directory, change_name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		if (errno == ENOENT)
			return Open::Absent;
		throw ChangeRefused("change file cannot be opened");
	}
	struct stat info{};
	const bool inspected = ::fstat(fd, &info) == 0;
	const auto words = inspected ? checked_words(info, publisher) : 0;
	if (!words || !internal::mode_only_acl(fd)) {
		::close(fd);
		throw ChangeRefused("change file failed its checks");
	}
	auto address = ::mmap(nullptr, words * 8, PROT_READ, MAP_SHARED, fd, 0);
	::close(fd);
	if (address == MAP_FAILED)
		throw ChangeRefused("change file cannot be mapped");
	ChangeMapping mapped;
	mapped.words_ = static_cast<const ChangeWord *>(address);
	mapped.count_ = words;
	mapped.device_ = std::uint64_t(info.st_dev);
	mapped.inode_ = std::uint64_t(info.st_ino);
	out = std::move(mapped);
	return Open::Mapped;
}
ChangeMapping::~ChangeMapping() {
	if (words_)
		::munmap(const_cast<ChangeWord *>(words_), count_ * 8);
}
ChangeMapping::ChangeMapping(ChangeMapping &&other) noexcept
    : words_(std::exchange(other.words_, nullptr)), count_(std::exchange(other.count_, 0)),
      device_(std::exchange(other.device_, 0)), inode_(std::exchange(other.inode_, 0)) {}
ChangeMapping &ChangeMapping::operator=(ChangeMapping &&other) noexcept {
	if (this != &other) {
		this->~ChangeMapping();
		words_ = std::exchange(other.words_, nullptr);
		count_ = std::exchange(other.count_, 0);
		device_ = std::exchange(other.device_, 0);
		inode_ = std::exchange(other.inode_, 0);
	}
	return *this;
}
} // namespace detent::local::detail
