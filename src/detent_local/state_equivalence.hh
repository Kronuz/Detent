#pragma once
#include "state_cursor.hh"
namespace detent::local::detail {
// Complete borrowed-state comparison retains both actual recovered generations.
class StateEquivalence {
  public:
	StateEquivalence(RecoveredGeneration, RecoveredGeneration, std::shared_ptr<distribution::StorageLedger>);
	void step(ValidationBudget &);
	bool done() const noexcept { return done_; }
	bool equal() const;

  private:
	std::optional<StateCursor> left_, right_;
	enum class Phase { Namespaces, Records, Overrides } phase_ = Phase::Namespaces;
	bool left_ready_ = false, right_ready_ = false, done_ = false, equal_ = true;
	std::size_t text_index_ = 0, text_offset_ = 0;
	bool texts(std::span<const std::pair<std::string_view, std::string_view>>, ValidationBudget &);
	void mismatch() noexcept {
		equal_ = false;
		done_ = true;
	}
};
} // namespace detent::local::detail
