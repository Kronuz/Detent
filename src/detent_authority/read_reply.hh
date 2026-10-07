#pragma once
#include "state.hh"
#include <memory>

namespace detent::authority {
// Borrows an immutable state and key list for both passes. The caller retains
// those objects until encoding or discard completes and owns root retirement.
class ReadReplyEncoder {
  public:
	ReadReplyEncoder(const State &state, std::string_view name_space, const std::vector<std::string> &keys,
					 Context context, std::size_t limit);
	~ReadReplyEncoder();
	ReadReplyEncoder(ReadReplyEncoder &&) noexcept;
	ReadReplyEncoder &operator=(ReadReplyEncoder &&) = delete;
	void step(CommandWorkBudget &budget);
	bool counted() const noexcept;
	std::size_t encoded_size() const;
	void begin_encoding();
	bool done() const noexcept;
	std::string take();
	bool discard(CommandWorkBudget &budget) noexcept;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::authority
