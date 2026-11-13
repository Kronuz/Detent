#pragma once
#include "paged_graph_validator.hh"
#include <chrono>
#include <array>
#include <filesystem>
#include <map>
#include <sha256.h>
#include <vector>

namespace detent::local::detail {
// Paced background scrub: hashes each pack against its owner record's checksum at a fixed byte rate, so a
// pass over B bytes takes B / rate seconds. It reads the pack file through the store directory (pread,
// uncached), not the agent's own mapping, so it sees what the file holds now. A pack that differs, is
// missing, has another length or cannot be read is reported, and its verified-page bits are withdrawn.
class PackScrub {
  public:
	using Clock = std::chrono::steady_clock;
	explicit PackScrub(std::uint64_t bytes_per_second, std::size_t slice_bytes = 64 * 1024);
	PackScrub(const PackScrub &) = delete;
	PackScrub &operator=(const PackScrub &) = delete;
	~PackScrub();
	// Starts a pass over `packs`, named in `directory`, and discards any pass in progress; the allowance
	// starts at zero.
	void begin(std::span<const SegmentInput> packs, Clock::time_point now, const std::filesystem::path &directory);
	// Retargets the pass in progress to another pack set: a pack this pass already hashed clean against the
	// same record is not hashed again, and the rest are. With no pass in progress it is `begin`.
	void rebase(std::span<const SegmentInput> packs, Clock::time_point now, const std::filesystem::path &directory);
	// Hashes what the pace allows up to `now`, at most `limit` bytes; returns the bytes hashed. Unspent
	// allowance carries to the next call. EMFILE, ENFILE and ENOMEM on a pack are not damage: the call throws
	// `std::system_error` and the pass stays at that pack for the next call.
	std::uint64_t step(Clock::time_point now, std::uint64_t limit = UINT64_MAX);
	bool running() const noexcept { return running_; }
	// Completed passes and bytes hashed over all passes. `failures()` is the packs whose checksum differed in
	// the last completed pass, in pack order; `begin` clears it, so damage is reported once per pass and a
	// new pack set starts clean.
	std::uint64_t passes() const noexcept { return passes_; }
	std::uint64_t bytes_hashed() const noexcept { return hashed_; }
	const std::vector<StorageIdentity> &failures() const noexcept { return completed_; }

  private:
	std::uint64_t rate_;
	std::size_t slice_;
	std::span<const SegmentInput> packs_;
	std::vector<char> buffer_;
	int directory_ = -1, file_ = -1;
	std::size_t next_ = 0, offset_ = 0;
	class SHA256 hash_;
	double credit_ = 0;
	Clock::time_point last_;
	std::uint64_t passes_ = 0, hashed_ = 0;
	std::vector<StorageIdentity> found_, completed_;
	std::map<StorageIdentity, std::array<char, 32>> verified_;
	bool running_ = false;
	void start(std::span<const SegmentInput> packs, Clock::time_point now, const std::filesystem::path &directory);
	bool verified(const SegmentInput &pack) const;
	void close_file() noexcept;
	bool open_file();
	void damaged();
	void next_pack();
};
} // namespace detent::local::detail
