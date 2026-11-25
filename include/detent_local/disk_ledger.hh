#pragma once
#include "generation.hh"
#include "ownership.hh"
#include "paged_layout.hh"
#include <algorithm>
#include <allocators/resources.h>
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
// against the census that publication left. allocators' ReservationBook keeps the arithmetic.
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
		if (book_ || fenced_)
			throw std::logic_error("disk census already initialized or fenced");
		book_.emplace(vector({limits_.bytes, limits_.entries, limits_.generations}), vector(complete_census));
	}
	// A generation's files beside its image: an empty `.lease`, `.read` and `.live` (`live_bytes`), and the
	// replacement table, staged under a `.tmp-` name before it is renamed over `selected`.
	static DiskUsage generation_peak(std::uint64_t image_length, std::uint64_t read_bytes, std::uint64_t live_bytes) {
		constexpr std::uint64_t table = 64 + 137 + 255 + 32;
		if (image_length < paged::root_bytes || read_bytes > (1u << 16) || live_bytes > (std::uint64_t(1) << 40) ||
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
		if (!book_ || fenced_)
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
		auto reservation = book_->try_reserve(vector(peak), book_->hard());
		if (!reservation)
			throw DiskPressure("local disk admission pressure");
		slots_[slot] = {++serial_, std::move(*reservation)};
		return {serial_, slot};
	}
	// Ends a reservation: its work wrote nothing (`census` empty) or left the store at `census`.
	void settle(Permit permit, std::optional<DiskUsage> census = {}) {
		auto &slot = checked(permit);
		book_->cancel(slot.reservation);
		slot = {};
		if (census)
			observe(*census);
	}
	// The store's census after work that only removes names (a collection), or after a settlement.
	void observe(DiskUsage census) {
		if (!book_ || fenced_)
			throw std::logic_error("disk accounting requires a healthy census");
		book_->recount(vector(census));
	}
	void fence() noexcept { fenced_ = true; }
	DiskStats stats() const noexcept {
		if (!book_)
			return {{}, {}, {}, false, fenced_};
		return {usage(book_->used()), usage(book_->reserved()), usage(book_->peak()), true, fenced_};
	}
	DiskLimits limits() const noexcept { return limits_; }

  private:
	using Vector = allocators::ResourceVector<3>;
	struct Slot {
		std::uint64_t serial = 0;
		allocators::Reservation<3> reservation;
	};
	static Vector vector(DiskUsage value) noexcept { return {{value.bytes, value.entries, value.generations}}; }
	static DiskUsage usage(Vector value) noexcept { return {value.values[0], value.values[1], value.values[2]}; }
	Slot &checked(Permit permit) {
		if (!permit.serial || permit.slot >= slots_.size() || slots_[permit.slot].serial != permit.serial)
			throw std::logic_error("stale disk reservation");
		return slots_[permit.slot];
	}
	DiskLimits limits_;
	bool fenced_ = false;
	std::optional<allocators::ReservationBook<3>> book_;
	std::array<Slot, 2> slots_{};
	std::uint64_t serial_ = 0;
};
} // namespace detent::local
