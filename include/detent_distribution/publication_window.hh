#pragma once
#include "detent/cursor.hh"
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace detent::distribution {
enum class PublicationFeedbackResult { Accepted, Busy, OutOfSync };
// Owner-local one-offer state. The host authenticates requests and retains
// transport originals; Marker belongs to its history/continuity adapter.
template <class Marker> class PublicationWindow {
	static_assert(std::is_nothrow_move_constructible_v<Marker> &&
				  std::is_nothrow_move_assignable_v<Marker>);

  public:
	struct Offer {
		std::uint64_t window;
		Cursor target;
		std::optional<Marker> marker;
	};
	PublicationWindow() = default;
	PublicationWindow(std::optional<Cursor> base, std::optional<Marker> marker, bool snapshot)
		: base_(std::move(base)), marker_(std::move(marker)), snapshot_(snapshot) {}
	std::uint64_t accepted() const noexcept { return accepted_; }
	const std::optional<Cursor> &base() const noexcept { return base_; }
	const std::optional<Marker> &marker() const noexcept { return marker_; }
	const std::optional<Offer> &offered() const noexcept { return offered_; }
	bool transferring() const noexcept { return transfer_; }
	bool snapshot() const noexcept { return snapshot_; }
	PublicationFeedbackResult feedback(std::uint64_t after,
									   const std::optional<Cursor> &published) const noexcept {
		if (transfer_ && (!offered_ || after != offered_->window || !published))
			return PublicationFeedbackResult::Busy;
		const bool valid =
			offered_ ? after == offered_->window && published && *published == offered_->target
					 : after == accepted_ &&
						   (!published || (accepted_ && base_ && *published == *base_));
		return valid ? PublicationFeedbackResult::Accepted : PublicationFeedbackResult::OutOfSync;
	}
	// The host calls this only for an admitted exact-feedback request.
	bool accept_settled() noexcept {
		if (transfer_)
			return false;
		if (offered_) {
			accepted_ = offered_->window;
			base_ = offered_->target;
			marker_ = std::move(offered_->marker);
			offered_.reset();
			snapshot_ = false;
		}
		return true;
	}
	std::uint64_t offer(Cursor target, std::optional<Marker> marker) {
		if (transfer_ || offered_ || accepted_ == std::numeric_limits<std::uint64_t>::max() ||
			(base_ && (!base_->same_identity(target) || target.revision < base_->revision)) ||
			(!snapshot_ && (!base_ || target.revision <= base_->revision)))
			throw std::logic_error("publication window offer state mismatch");
		offered_.emplace(Offer{accepted_ + 1, target, std::move(marker)});
		transfer_ = true;
		return offered_->window;
	}
	bool settle(std::uint64_t window) noexcept {
		if (!transfer_ || !offered_ || offered_->window != window)
			return false;
		transfer_ = false;
		return true;
	}

  private:
	std::uint64_t accepted_ = 0;
	std::optional<Cursor> base_;
	std::optional<Marker> marker_;
	std::optional<Offer> offered_;
	bool snapshot_ = true, transfer_ = false;
};
} // namespace detent::distribution
