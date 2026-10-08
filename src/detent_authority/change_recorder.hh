#pragma once
#include <cstdint>
#include <memory>
#include <span>
namespace detent::authority {
class State;
struct Command;
struct Context;
struct CommandWorkBudget;
// Optional delivery history borrows one unpublished application until it has
// materialized independent bytes. Refusal cannot reject authoritative apply.
class AppliedChangeDraft {
  public:
	virtual ~AppliedChangeDraft() = default;
	virtual bool step(CommandWorkBudget &budget) = 0;
	virtual bool discard(CommandWorkBudget &budget) noexcept = 0;
};
class AppliedChangeRecorder {
  public:
	virtual ~AppliedChangeRecorder() = default;
	virtual void begin_batch(const Context &context, std::uint64_t index,
							 std::uint64_t base_revision) noexcept = 0;
	virtual std::shared_ptr<AppliedChangeDraft> capture(const State &candidate, const Command &command,
														std::span<const unsigned char> changed) = 0;
	virtual void stage(std::shared_ptr<AppliedChangeDraft> draft) noexcept = 0;
	virtual void gap(std::uint64_t revision) noexcept = 0;
	virtual void publish(const Context &context, std::uint64_t index, std::uint64_t revision) noexcept = 0;
	virtual void abandon_batch() noexcept = 0;
	virtual void reset(const Context &context, std::uint64_t revision) noexcept = 0;
	virtual bool pending() const noexcept = 0;
	virtual void collect(CommandWorkBudget &budget) noexcept = 0;
};
} // namespace detent::authority
