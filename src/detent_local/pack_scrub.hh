#pragma once
#include "paged_graph_validator.hh"
#include <chrono>
#include <sha256.h>
#include <vector>

namespace detent::local::detail {
// Paced background scrub: hashes each pack against its owner record's checksum at a fixed byte rate, so a
// pass over B bytes takes B / rate seconds. The owner keeps the packs mapped while a pass runs.
class PackScrub {
  public:
	using Clock = std::chrono::steady_clock;
	explicit PackScrub(std::uint64_t bytes_per_second, std::size_t slice_bytes = 64 * 1024);
	// Starts a pass over `packs` and discards any pass in progress; the allowance starts at zero.
	void begin(std::span<const SegmentInput> packs, Clock::time_point now);
	// Hashes what the pace allows up to `now`, at most `limit` bytes; returns the bytes hashed. Unspent
	// allowance carries to the next call.
	std::uint64_t step(Clock::time_point now, std::uint64_t limit = UINT64_MAX);
	bool running() const noexcept { return running_; }
	// Completed passes, bytes hashed over all passes, and the packs whose checksum differed, in pack order.
	std::uint64_t passes() const noexcept { return passes_; }
	std::uint64_t bytes_hashed() const noexcept { return hashed_; }
	const std::vector<StorageIdentity> &failures() const noexcept { return failures_; }

  private:
	std::uint64_t rate_;
	std::size_t slice_;
	std::span<const SegmentInput> packs_;
	std::size_t next_ = 0, offset_ = 0;
	class SHA256 hash_;
	double credit_ = 0;
	Clock::time_point last_;
	std::uint64_t passes_ = 0, hashed_ = 0;
	std::vector<StorageIdentity> failures_;
	bool running_ = false;
};
} // namespace detent::local::detail
