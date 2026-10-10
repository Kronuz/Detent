#pragma once
#include "framing.hh"

namespace detent::distribution {
// One chunk and one owned frame at a time. Stream originals and semantic-source
// retirement belong to the host; taking a frame only releases encoding input.
class FramePump {
  public:
	FramePump(UpdateKind kind, Identity update, std::uint64_t total, bool compress,
			  std::shared_ptr<StorageLedger> account)
		: encoder_(kind, update, total, compress, std::move(account)), total_(total) {}
	bool wants_input() const noexcept { return !active_ && !finished_ && !retiring_; }
	void begin(std::shared_ptr<const OwnedBuffer> input) {
		if (!wants_input() || !input || accepted_ > total_ || input->size() > total_ - accepted_)
			throw std::logic_error("frame pump input state mismatch");
		const auto length = input->size();
		encoder_.begin(std::move(input), length == total_ - accepted_);
		accepted_ += length;
		active_ = true;
	}
	void step(WorkBudget &budget) {
		if (retiring_)
			throw std::logic_error("frame pump retiring");
		if (!finished_)
			encoder_.step(budget);
	}
	bool ready() const noexcept { return !retiring_ && encoder_.ready(); }
	std::shared_ptr<const OwnedBuffer> take() {
		if (!ready())
			throw std::logic_error("frame pump output unavailable");
		auto result = encoder_.take();
		active_ = false;
		finished_ = accepted_ == total_;
		return result;
	}
	bool done() const noexcept { return finished_; }
	bool discard(WorkBudget &budget) noexcept {
		retiring_ = true;
		if (!encoder_.discard(budget))
			return false;
		active_ = false;
		finished_ = true;
		return true;
	}

  private:
	OwnedFragmentEncoder encoder_;
	const std::uint64_t total_;
	std::uint64_t accepted_ = 0;
	bool active_ = false, finished_ = false, retiring_ = false;
};
} // namespace detent::distribution
