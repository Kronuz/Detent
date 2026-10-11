#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace detent::delivery {
// Equal jitter keeps a positive retry floor and bounds exponential growth.
class ReconnectBackoff {
  public:
	ReconnectBackoff(std::chrono::milliseconds initial, std::chrono::milliseconds maximum,
					 std::uint64_t seed) noexcept
		: initial_(initial), maximum_(maximum), ceiling_(initial), random_(seed) {}
	void reset() noexcept { ceiling_ = initial_; }
	std::chrono::milliseconds next() noexcept {
		if (maximum_ == std::chrono::milliseconds::zero())
			return initial_;
		const auto ceiling = std::min(ceiling_, maximum_).count();
		const auto floor = std::max<std::int64_t>(1, ceiling / 2);
		random_ += UINT64_C(0x9e3779b97f4a7c15);
		auto bits = random_;
		bits = (bits ^ (bits >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
		bits = (bits ^ (bits >> 27)) * UINT64_C(0x94d049bb133111eb);
		bits ^= bits >> 31;
		ceiling_ = std::chrono::milliseconds(ceiling > maximum_.count() / 2 ? maximum_.count() : ceiling * 2);
		return std::chrono::milliseconds(floor + bits % (ceiling - floor + 1));
	}

  private:
	std::chrono::milliseconds initial_, maximum_, ceiling_;
	std::uint64_t random_;
};
} // namespace detent::delivery
