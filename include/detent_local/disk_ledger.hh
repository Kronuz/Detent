#pragma once
#include "generation.hh"
#include "ownership.hh"
#include "paged_layout.hh"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace detent::local {
struct DiskUsage {
	std::uint64_t bytes = 0, entries = 0, generations = 0;
	bool operator==(const DiskUsage &) const = default;
};
struct DiskLimits {
	std::uint64_t bytes = 0, entries = 0, generations = 0;
};
struct DiskStats {
	DiskUsage used, reserved, peak;
	bool initialized = false, fenced = false;
};
class DiskPressure final : public std::length_error {
  public:
	explicit DiskPressure(const char *message) : std::length_error(message) {}
};

// Single-executor logical named-space accounting, independent of heap and
// physical blocks. The session owns this fixed registry through every original.
// Permits never refund on destruction: only explicit, proved settlement does.
class DiskLedger {
  public:
	struct Permit {
		std::uint64_t serial = 0;
		std::size_t slot = 0;
		bool operator==(const Permit &) const = default;
	};
	explicit DiskLedger(DiskLimits limits) : limits_(limits) {}
	DiskLedger(const DiskLedger &) = delete;
	DiskLedger &operator=(const DiskLedger &) = delete;
	void initialize(DiskUsage complete_census) {
		if (stats_.initialized || stats_.fenced)
			throw std::logic_error("disk census already initialized or fenced");
		stats_.used = stats_.peak = complete_census; // Retain complete over-quota debt.
		stats_.initialized = true;
	}
	static DiskUsage generation_peak(std::uint64_t image_length) {
		constexpr auto overhead = generation_ownership_bytes + descriptor_bytes;
		if (image_length < header_bytes ||
			image_length > std::numeric_limits<std::uint64_t>::max() - overhead)
			throw std::length_error("invalid managed generation extent");
		return {image_length + overhead, 3, 1};
	}
	static DiskUsage paged_peak(std::span<const std::uint64_t> pack_lengths,
	                           ReaderMetadataEncoding encoding = ReaderMetadataEncoding::Legacy) {
		DiskUsage peak{paged::root_bytes + generation_ownership_bytes + descriptor_bytes +
		                   reader_metadata_length(encoding),
		               4, 1};
		for (const auto length : pack_lengths) {
			if (!length || length > paged::segment_bytes ||
				length > std::numeric_limits<std::uint64_t>::max() - segment_ownership_bytes - peak.bytes ||
				peak.entries > std::numeric_limits<std::uint64_t>::max() - 2)
				throw std::length_error("invalid managed paged extent");
			peak.bytes += length + segment_ownership_bytes;
			peak.entries += 2;
		}
		return peak;
	}
	Permit reserve(DiskUsage peak) {
		if (!stats_.initialized || stats_.fenced)
			throw std::logic_error("disk admission requires a complete healthy census");
		if (peak == DiskUsage{})
			throw std::invalid_argument("empty disk reservation");
		if (!fits(stats_.used.bytes, stats_.reserved.bytes, peak.bytes, limits_.bytes) ||
			!fits(stats_.used.entries, stats_.reserved.entries, peak.entries, limits_.entries) ||
			!fits(stats_.used.generations, stats_.reserved.generations, peak.generations,
				  limits_.generations))
			throw DiskPressure("local disk admission pressure");
		std::size_t slot = 0;
		while (slot < slots_.size() && slots_[slot].serial)
			++slot;
		if (slot == slots_.size())
			throw DiskPressure("local disk active registry pressure");
		if (serial_ == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("disk reservation identity exhausted");
		auto reserved = add(stats_.reserved, peak);
		auto total = add(stats_.used, reserved);
		slots_[slot] = {++serial_, peak, 0, false};
		stats_.reserved = reserved;
		stats_.peak.bytes = std::max(stats_.peak.bytes, total.bytes);
		stats_.peak.entries = std::max(stats_.peak.entries, total.entries);
		stats_.peak.generations = std::max(stats_.peak.generations, total.generations);
		return {serial_, slot};
	}
	// Call only after a driver accepts the original; rejection changes nothing.
	void accepted(Permit permit) {
		auto &slot = checked(permit);
		if (slot.originals == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("disk original count exhausted");
		++slot.originals;
		slot.started = true;
	}
	// Call exactly once for an authentic, token-matched original completion.
	void completed(Permit permit) {
		auto &slot = checked(permit);
		if (!slot.originals)
			throw std::logic_error("disk completion without an original");
		--slot.originals;
	}
	void cancel_unstarted(Permit permit) {
		auto &slot = checked(permit);
		if (slot.started || slot.originals)
			throw std::logic_error("started disk reservation requires proved settlement");
		release(slot);
	}
	// The caller proves named observations and required barriers, including any
	// previous selected descriptor credit. No exception or EEXIST is absence.
	void settle(Permit permit, DiskUsage observed, DiskUsage durable_credit = {}) {
		auto &slot = checked(permit);
		if (stats_.fenced || !slot.started || slot.originals)
			throw std::logic_error("disk settlement requires healthy completed originals");
		if (!within(observed, slot.reserved)) {
			stats_.fenced = true;
			throw std::logic_error("disk effects exceed their reservation");
		}
		// Credit existing named debt only, never an invented future observation.
		auto used = add(subtract(stats_.used, durable_credit), observed);
		stats_.used = used;
		release(slot);
	}
	// GC calls this only after both namespace barriers and held-file closure.
	// An over-quota store may reclaim; uncertain/fenced stores must recover.
	void reclaimed(DiskUsage actual_deleted) {
		if (!stats_.initialized || stats_.fenced)
			throw std::logic_error("disk reclamation requires a healthy census");
		stats_.used = subtract(stats_.used, actual_deleted);
	}
	void fence() noexcept { stats_.fenced = true; }
	DiskStats stats() const noexcept { return stats_; }
	DiskLimits limits() const noexcept { return limits_; }
	std::uint64_t originals(Permit permit) const { return checked(permit).originals; }

  private:
	struct Slot {
		std::uint64_t serial = 0;
		DiskUsage reserved;
		std::uint64_t originals = 0;
		bool started = false;
	};
	static bool fits(std::uint64_t used, std::uint64_t reserved, std::uint64_t extra, std::uint64_t limit) {
		return used <= limit && reserved <= limit - used && extra <= limit - used - reserved;
	}
	static bool within(DiskUsage value, DiskUsage ceiling) {
		return value.bytes <= ceiling.bytes && value.entries <= ceiling.entries &&
			   value.generations <= ceiling.generations;
	}
	static std::uint64_t sum(std::uint64_t a, std::uint64_t b) {
		if (b > std::numeric_limits<std::uint64_t>::max() - a)
			throw std::overflow_error("disk accounting overflow");
		return a + b;
	}
	static DiskUsage add(DiskUsage a, DiskUsage b) {
		return {sum(a.bytes, b.bytes), sum(a.entries, b.entries), sum(a.generations, b.generations)};
	}
	static DiskUsage subtract(DiskUsage a, DiskUsage b) {
		if (!within(b, a))
			throw std::logic_error("disk accounting credit exceeds observed debt");
		return {a.bytes - b.bytes, a.entries - b.entries, a.generations - b.generations};
	}
	Slot &checked(Permit permit) {
		if (!permit.serial || permit.slot >= slots_.size() || slots_[permit.slot].serial != permit.serial)
			throw std::logic_error("stale disk reservation");
		return slots_[permit.slot];
	}
	const Slot &checked(Permit permit) const { return const_cast<DiskLedger *>(this)->checked(permit); }
	void release(Slot &slot) {
		stats_.reserved = subtract(stats_.reserved, slot.reserved);
		slot = {};
	}
	DiskLimits limits_;
	DiskStats stats_;
	std::array<Slot, 2> slots_{};
	std::uint64_t serial_ = 0;
};
} // namespace detent::local
