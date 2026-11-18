#include "pack_scrub.hh"
#include "segment_names.hh"
#include "verified_pages.hh"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::local::detail {
namespace {
// Resource exhaustion says nothing about the pack: the step throws and the pass stays where it is.
bool transient(int error) noexcept { return error == EMFILE || error == ENFILE || error == ENOMEM; }
} // namespace
PackScrub::PackScrub(std::uint64_t bytes_per_second, std::size_t slice_bytes)
	: rate_(bytes_per_second), slice_(slice_bytes), buffer_(slice_bytes) {
	if (!rate_ || !slice_)
		throw std::invalid_argument("scrub rate and slice must be positive");
}
PackScrub::~PackScrub() {
	close_file();
	if (directory_ >= 0)
		::close(directory_);
}
void PackScrub::close_file() noexcept {
	if (file_ >= 0)
		::close(file_);
	file_ = -1;
}
void PackScrub::begin(std::span<const SegmentInput> packs, Clock::time_point now,
					  const std::filesystem::path &directory) {
	verified_.clear();
	start(packs, now, directory);
}
void PackScrub::rebase(std::span<const SegmentInput> packs, Clock::time_point now,
					   const std::filesystem::path &directory) {
	std::map<SegmentId, std::array<char, 32>> kept;
	if (running_)
		for (const auto &pack : packs)
			if (auto found = verified_.find(pack.ownership.segment);
				found != verified_.end() && found->second == pack.ownership.checksum)
				kept.insert(*found);
	verified_ = std::move(kept);
	start(packs, now, directory);
}
void PackScrub::start(std::span<const SegmentInput> packs, Clock::time_point now,
					  const std::filesystem::path &directory) {
	close_file();
	found_.clear();
	completed_.clear();
	if (directory_ >= 0)
		::close(directory_);
	directory_ = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (directory_ < 0)
		directory_ = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (directory_ < 0) {
		running_ = false;
		throw std::system_error(errno, std::generic_category(), "open scrub directory");
	}
	packs_ = packs;
	next_ = offset_ = 0;
	credit_ = 0;
	last_ = now;
	hash_.reset();
	running_ = !packs.empty();
	if (!running_)
		++passes_;
}
// Opens the current pack for reading around the page cache; false when it is missing, not a file of the
// owner record's length, or cannot be opened.
bool PackScrub::open_file() {
	if (directory_ < 0)
		return false;
	const auto &pack = packs_[next_];
	const SegmentNames names(pack.ownership.segment);
	const auto name = std::string(names.payload_name());
	file_ = ::openat(directory_, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	struct stat info{};
	int error = file_ < 0 ? errno : 0;
	if (!error && ::fstat(file_, &info) != 0)
		error = errno;
	if (transient(error)) {
		close_file();
		throw std::system_error(error, std::generic_category(), "open scrub pack");
	}
	if (error || !S_ISREG(info.st_mode) || std::uint64_t(info.st_size) != pack.bytes.size()) {
		close_file();
		return false;
	}
#ifdef F_NOCACHE
	::fcntl(file_, F_NOCACHE, 1);
#endif
	return true;
}
// True when this pass already hashed the pack, byte for byte, against the same record.
bool PackScrub::verified(const SegmentInput &pack) const {
	const auto found = verified_.find(pack.ownership.segment);
	return found != verified_.end() && found->second == pack.ownership.checksum;
}
// The current pack failed: report it, withdraw its verified pages and move on.
void PackScrub::damaged() {
	const auto &pack = packs_[next_];
	found_.push_back(pack.ownership.segment);
	if (pack.pages)
		pack.pages->clear();
}
void PackScrub::next_pack() {
	close_file();
	hash_.reset();
	offset_ = 0;
	if (++next_ == packs_.size()) {
		running_ = false;
		++passes_;
		completed_ = std::move(found_);
		found_.clear();
	}
}
std::uint64_t PackScrub::step(Clock::time_point now, std::uint64_t limit) {
	if (!running_)
		return 0;
	if (now > last_) {
		const std::chrono::duration<double> elapsed = now - last_;
		// A late caller catches up at most one second of allowance, so a stalled caller is not repaid in a burst.
		credit_ = std::min(credit_ + elapsed.count() * double(rate_), double(rate_));
		last_ = now;
	}
	std::uint64_t done = 0;
	while (running_) {
		const auto &pack = packs_[next_];
		if (file_ < 0 && offset_ == 0 && verified(pack)) {
			next_pack();
			continue;
		}
		if (file_ < 0 && offset_ == 0 && !open_file() && !open_file()) {
			damaged();
			next_pack();
			continue;
		}
		const auto remaining = pack.bytes.size() - offset_;
		if (done >= limit)
			break;
		const auto allowed = std::uint64_t(credit_);
		const auto count = std::min<std::uint64_t>({remaining, slice_, allowed, limit - done});
		if (!count && remaining)
			break;
		if (count) {
			const auto read = [&] {
				const auto got = ::pread(file_, buffer_.data(), count, static_cast<off_t>(offset_));
				return got == static_cast<ssize_t>(count) ? 0 : got < 0 ? errno : EIO;
			};
			int error = read();
			if (error && !transient(error))
				error = read();
			if (transient(error))
				throw std::system_error(error, std::generic_category(), "read scrub pack");
			if (error) {
				damaged();
				next_pack();
				continue;
			}
#ifdef POSIX_FADV_DONTNEED
			::posix_fadvise(file_, static_cast<off_t>(offset_), static_cast<off_t>(count), POSIX_FADV_DONTNEED);
#endif
			hash_.add(buffer_.data(), count);
			offset_ += count;
			credit_ -= double(count);
			done += count;
			hashed_ += count;
		}
		if (offset_ != pack.bytes.size())
			continue;
		unsigned char digest[32];
		hash_.getHash(digest);
		if (std::memcmp(digest, pack.ownership.checksum.data(), 32) != 0)
			damaged();
		else
			verified_.emplace(pack.ownership.segment, pack.ownership.checksum);
		next_pack();
	}
	return done;
}
} // namespace detent::local::detail
