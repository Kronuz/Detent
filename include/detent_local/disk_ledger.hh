#pragma once
#include "generation.hh"
#include "ownership.hh"
#include "paged_layout.hh"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
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

// Single-executor logical named-space accounting, independent of heap and physical blocks. `used` is
// the agent store's census of its names; a reservation covers one publication's peak until it settles
// against the census that publication left.
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
	// A generation's files beside its image: an empty `.lease`, `.read` and `.live` (`live_bytes`), and the
	// replacement table, staged under a `.tmp-` name before it is renamed over `selected`.
	static DiskUsage generation_peak(std::uint64_t image_length, std::uint64_t read_bytes, std::uint64_t live_bytes) {
		constexpr std::uint64_t table = 64 + 137 + 255 + 32;
		if (image_length < header_bytes || read_bytes > (1u << 16) || live_bytes > (std::uint64_t(1) << 40) ||
			image_length > std::numeric_limits<std::uint64_t>::max() - table - read_bytes - live_bytes)
			throw std::length_error("invalid managed generation extent");
		return {image_length + read_bytes + live_bytes + table, 5, 1};
	}
	// `live_bytes` is the size of the generation's live record, which covers every segment it reaches.
	static DiskUsage paged_peak(std::span<const std::uint64_t> pack_lengths, std::uint64_t live_bytes) {
		auto peak = generation_peak(paged::root_bytes, prepared_metadata_bytes, live_bytes);
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
		slots_[slot] = {++serial_, peak};
		stats_.reserved = reserved;
		stats_.peak.bytes = std::max(stats_.peak.bytes, total.bytes);
		stats_.peak.entries = std::max(stats_.peak.entries, total.entries);
		stats_.peak.generations = std::max(stats_.peak.generations, total.generations);
		return {serial_, slot};
	}
	// Ends a reservation: its work wrote nothing (`census` empty) or left the store at `census`.
	void settle(Permit permit, std::optional<DiskUsage> census = {}) {
		auto &slot = checked(permit);
		stats_.reserved = subtract(stats_.reserved, slot.reserved);
		slot = {};
		if (census)
			observe(*census);
	}
	// The store's census after work that only removes names (a collection), or after a settlement.
	void observe(DiskUsage census) {
		if (!stats_.initialized || stats_.fenced)
			throw std::logic_error("disk accounting requires a healthy census");
		stats_.used = census;
		const auto total = add(stats_.used, stats_.reserved);
		stats_.peak.bytes = std::max(stats_.peak.bytes, total.bytes);
		stats_.peak.entries = std::max(stats_.peak.entries, total.entries);
		stats_.peak.generations = std::max(stats_.peak.generations, total.generations);
	}
	void fence() noexcept { stats_.fenced = true; }
	DiskStats stats() const noexcept { return stats_; }
	DiskLimits limits() const noexcept { return limits_; }

  private:
	struct Slot {
		std::uint64_t serial = 0;
		DiskUsage reserved;
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
	DiskLimits limits_;
	DiskStats stats_;
	std::array<Slot, 2> slots_{};
	std::uint64_t serial_ = 0;
};
} // namespace detent::local
