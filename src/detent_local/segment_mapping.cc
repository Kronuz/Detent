#include "segment_mapping.hh"
#include "detent_local/paged_layout.hh"
#include "generation_mapping.hh"
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::local::detail {
namespace {
SegmentFileIdentity inspect_mapping_file(int fd, uid_t owner, const SegmentOwnership &ownership) {
	struct stat info{};
	if (fd < 0 || ::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != owner ||
		info.st_nlink != 1 || (info.st_mode & 0022) || info.st_size < 0 ||
		std::uint64_t(info.st_size) != ownership.length)
		throw std::invalid_argument("invalid immutable segment file");
	return {std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)};
}
} // namespace
SegmentMapping::SegmentMapping(std::shared_ptr<distribution::StorageLedger> account, int fd, uid_t owner,
							   SegmentOwnership ownership)
	: account_(std::move(account)), ownership_(ownership) {
	encode_segment_ownership(ownership_);
	const auto identity = inspect_mapping_file(fd, owner, ownership_);
	if (!account_)
		throw std::invalid_argument("invalid immutable segment mapping admission");
	ticket_ = account_->admit(rounded_mapping_bytes(ownership_.length));
	install_checked(fd, identity);
}
SegmentMapping::SegmentMapping(std::shared_ptr<distribution::StorageLedger> account,
							   SegmentOwnership ownership)
	: account_(std::move(account)), ownership_(ownership) {
	encode_segment_ownership(ownership_);
	if (!account_)
		throw std::invalid_argument("invalid immutable segment mapping admission");
	ticket_ = account_->admit(rounded_mapping_bytes(ownership_.length));
}
void SegmentMapping::install(int fd, uid_t owner) {
	if (mapped_ || !ticket_.bytes())
		throw std::logic_error("segment mapping installation unavailable");
	install_checked(fd, inspect_mapping_file(fd, owner, ownership_));
}
void SegmentMapping::install_checked(int fd, SegmentFileIdentity identity) {
	auto mapped = ::mmap(nullptr, ownership_.length, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapped == MAP_FAILED)
		throw std::runtime_error("immutable segment mmap failed");
	address_ = static_cast<char *>(mapped);
	mapped_ = true;
	identity_ = identity;
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
