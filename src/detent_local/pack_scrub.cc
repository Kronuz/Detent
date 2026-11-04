#include "pack_scrub.hh"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace detent::local::detail {
PackScrub::PackScrub(std::uint64_t bytes_per_second, std::size_t slice_bytes)
	: rate_(bytes_per_second), slice_(slice_bytes) {
	if (!rate_ || !slice_)
		throw std::invalid_argument("scrub rate and slice must be positive");
}
void PackScrub::begin(std::span<const SegmentInput> packs, Clock::time_point now) {
	packs_ = packs;
	next_ = offset_ = 0;
	credit_ = 0;
	last_ = now;
	hash_.reset();
	found_.clear();
	completed_.clear();
	running_ = !packs.empty();
	if (!running_)
		++passes_;
}
std::uint64_t PackScrub::step(Clock::time_point now, std::uint64_t limit) {
	if (!running_)
		return 0;
	if (now > last_) {
		const std::chrono::duration<double> elapsed = now - last_;
		// A late caller catches up at most one second of allowance, so a stall is not repaid in a burst.
		credit_ = std::min(credit_ + elapsed.count() * double(rate_), double(rate_));
		last_ = now;
	}
	std::uint64_t done = 0;
	while (running_) {
		const auto &pack = packs_[next_];
		const auto remaining = pack.bytes.size() - offset_;
		if (done >= limit)
			break;
		const auto allowed = std::uint64_t(credit_);
		const auto count = std::min<std::uint64_t>({remaining, slice_, allowed, limit - done});
		if (!count && remaining)
			break;
		if (count) {
			hash_.add(pack.bytes.data() + offset_, count);
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
			found_.push_back(pack.ownership.segment);
		hash_.reset();
		offset_ = 0;
		if (++next_ == packs_.size()) {
			running_ = false;
			++passes_;
			completed_ = std::move(found_);
			found_.clear();
		}
	}
	return done;
}
} // namespace detent::local::detail
