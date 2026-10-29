#pragma once
#include "generation.hh"
#include "ownership.hh"
#include "paged_layout.hh"
#include <algorithm>
#include <allocators/resources.h>
#include <optional>
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
		if (initialized_ || fenced_)
			throw std::logic_error("disk census already initialized or fenced");
		book_.emplace(vector({limits_.bytes, limits_.entries, limits_.generations}), vector(complete_census));
		initialized_ = true;
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
		if (!initialized_ || fenced_)
			throw std::logic_error("disk admission requires a complete healthy census");
		if (peak == DiskUsage{})
			throw std::invalid_argument("empty disk reservation");
		std::size_t slot = 0;
		while (slot < slots_.size() && slots_[slot].serial)
			++slot;
		if (slot == slots_.size())
			throw DiskPressure("local disk active registry pressure");
		if (serial_ == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("disk reservation identity exhausted");
		auto reservation = book_->try_reserve(vector(peak), vector({limits_.bytes, limits_.entries, limits_.generations}));
		if (!reservation)
			throw DiskPressure("local disk admission pressure");
		slots_[slot] = {++serial_, std::move(reservation), 0, false};
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
		book_->cancel(*slot.reservation);
		slot = {};
	}
	// The caller proves named observations and required barriers, including any
	// previous selected descriptor credit. No exception or EEXIST is absence.
	void settle(Permit permit, DiskUsage observed, DiskUsage durable_credit = {}) {
		auto &slot = checked(permit);
		if (fenced_ || !slot.started || slot.originals)
			throw std::logic_error("disk settlement requires healthy completed originals");
		if (!vector(observed).fits(slot.reservation->peak())) {
			fenced_ = true;
			throw std::logic_error("disk effects exceed their reservation");
		}
		// Credit existing named debt only, never an invented future observation.
		book_->settle(*slot.reservation, vector(observed), vector(durable_credit));
		slot = {};
	}
	// GC calls this only after both namespace barriers and held-file closure.
	// An over-quota store may reclaim; uncertain/fenced stores must recover.
	void reclaimed(DiskUsage actual_deleted) {
		if (!initialized_ || fenced_)
			throw std::logic_error("disk reclamation requires a healthy census");
		book_->reclaim(vector(actual_deleted));
	}
	void fence() noexcept { fenced_ = true; }
	DiskStats stats() const noexcept {
		if (!book_) return {{}, {}, {}, initialized_, fenced_};
		return {usage(book_->used()), usage(book_->reserved()), usage(book_->peak()), initialized_, fenced_};
	}
	DiskLimits limits() const noexcept { return limits_; }
	std::uint64_t originals(Permit permit) const { return checked(permit).originals; }

  private:
	struct Slot {
		std::uint64_t serial = 0;
		std::optional<allocators::Reservation<3>> reservation;
		std::uint64_t originals = 0;
		bool started = false;
	};
	using Vector = allocators::ResourceVector<3>;
	static Vector vector(DiskUsage value) { return {{{value.bytes, value.entries, value.generations}}}; }
	static DiskUsage usage(Vector value) noexcept {
		return {value.values[0], value.values[1], value.values[2]};
	}
	Slot &checked(Permit permit) {
		if (!permit.serial || permit.slot >= slots_.size() || slots_[permit.slot].serial != permit.serial)
			throw std::logic_error("stale disk reservation");
		return slots_[permit.slot];
	}
	const Slot &checked(Permit permit) const { return const_cast<DiskLedger *>(this)->checked(permit); }
	DiskLimits limits_;
	bool initialized_ = false, fenced_ = false;
	std::optional<allocators::ReservationBook<3>> book_;
	std::array<Slot, 2> slots_{};
	std::uint64_t serial_ = 0;
};
} // namespace detent::local
