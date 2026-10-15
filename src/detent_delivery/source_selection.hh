#pragma once
#include <algorithm>
#include <array>
#include <asio/ip/tcp.hpp>
#include <chrono>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace detent::delivery::detail {
class SourceSelection {
  public:
	using Clock = std::chrono::steady_clock;
	using Endpoint = asio::ip::tcp::endpoint;
	static constexpr std::size_t maximum_endpoints = 8;
	SourceSelection(std::span<const Endpoint> pool, Endpoint fallback, std::chrono::milliseconds refresh,
					std::uint64_t seed)
		: count_(pool.empty() ? 1 : pool.size()), pooled_(!pool.empty()), refresh_(refresh), random_(seed) {
		if (count_ > maximum_endpoints || refresh < std::chrono::seconds(1) ||
			refresh > std::chrono::hours(24))
			throw std::invalid_argument("invalid source selection bounds");
		if (pool.empty()) {
			endpoints_[0] = fallback;
			return;
		}
		for (std::size_t i = 0; i != count_; ++i) {
			const auto &endpoint = pool[i];
			if (!endpoint.port() || endpoint.address().is_unspecified() ||
				endpoint.address().is_multicast() ||
				std::find(pool.begin(), pool.begin() + i, endpoint) != pool.begin() + i)
				throw std::invalid_argument("invalid or duplicate source endpoint");
			endpoints_[i] = endpoint;
		}
	}
	const Endpoint &endpoint() const noexcept { return endpoints_[current_]; }
	bool pooled() const noexcept { return pooled_; }
	std::size_t index() const noexcept { return current_; }
	// True completes a failed round and requires reconnect backoff.
	bool unavailable() noexcept {
		if (!pooled_)
			return true;
		current_ = current_ + 1 == count_ ? 0 : current_ + 1;
		if (++attempts_ == count_) {
			attempts_ = 0;
			return true;
		}
		return false;
	}
	void successful() noexcept { attempts_ = 0; }
	void opened(Clock::time_point now) noexcept {
		random_ += UINT64_C(0x9e3779b97f4a7c15);
		auto bits = random_;
		bits = (bits ^ (bits >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
		bits = (bits ^ (bits >> 27)) * UINT64_C(0x94d049bb133111eb);
		bits ^= bits >> 31;
		const auto spread = refresh_.count() / 5;
		refresh_at_ = now + refresh_ - std::chrono::milliseconds(spread) +
					  std::chrono::milliseconds(bits % (2 * spread + 1));
	}
	bool refresh_due(Clock::time_point now) const noexcept { return pooled_ && now >= refresh_at_; }
	std::chrono::milliseconds wait(Clock::time_point now, std::chrono::milliseconds maximum) const noexcept {
		if (!pooled_)
			return maximum;
		return std::min(maximum,
						std::max(std::chrono::milliseconds::zero(),
								 std::chrono::duration_cast<std::chrono::milliseconds>(refresh_at_ - now)));
	}

  private:
	std::array<Endpoint, maximum_endpoints> endpoints_;
	std::size_t count_, current_ = 0, attempts_ = 0;
	bool pooled_;
	std::chrono::milliseconds refresh_;
	std::uint64_t random_;
	Clock::time_point refresh_at_ = Clock::time_point::max();
};
} // namespace detent::delivery::detail
