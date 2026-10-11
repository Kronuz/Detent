#include "segment_mapping.hh"
#include "detent_local/paged_layout.hh"
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::local::detail {
SegmentMapping::SegmentMapping(std::shared_ptr<distribution::StorageLedger> account, int fd, uid_t owner,
							   SegmentOwnership ownership)
	: account_(account), ownership_(ownership) {
	// Check codec invariants before reserving or mapping the complete file.
	encode_segment_ownership(ownership_);
	struct stat info{};
	if (fd < 0 || ::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != owner ||
		info.st_nlink != 1 || (info.st_mode & 0022) || info.st_size < 0 ||
		std::uint64_t(info.st_size) != ownership_.length)
		throw std::invalid_argument("invalid immutable segment file");
	auto page = ::sysconf(_SC_PAGESIZE);
	if (!account || page <= 0 ||
		ownership_.length > std::numeric_limits<std::size_t>::max() - (std::size_t(page) - 1))
		throw std::invalid_argument("invalid immutable segment mapping admission");
	auto capacity = ((ownership_.length + page - 1) / page) * page;
	ticket_ = account->admit(capacity);
	auto mapped = ::mmap(nullptr, ownership_.length, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapped == MAP_FAILED)
		throw std::runtime_error("immutable segment mmap failed");
	address_ = static_cast<char *>(mapped);
	mapped_ = true;
	identity_ = {std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)};
}
bool SegmentMapping::retire_step() noexcept {
	retry_ = false;
	if (mapped_) {
		if (::munmap(address_, ownership_.length) != 0) {
			retry_ = true;
			return false;
		}
		address_ = nullptr;
		mapped_ = false;
		ticket_.reset();
		return false;
	}
	return true;
}
} // namespace detent::local::detail
