#pragma once
#include "detent_distribution/protocol.hh"
#include "detent_distribution/retirement.hh"
#include <variant>

namespace detent::distribution::detail {
template <class T> class RetiredInput final : public RetirementNode {
  public:
	explicit RetiredInput(T &&input) : value(std::move(input)) {}
	T value;

  protected:
	bool retire_step() noexcept override {
		WorkBudget budget;
		return value.discard(budget);
	}
};
// Shared input is borrowed; its caller retains backing storage through staging.
// Transferred input queues page retirement when its final handle disappears.
template <class T> class SourceInput {
  public:
	explicit SourceInput(std::shared_ptr<const T> value) : value_(std::move(value)) {}
	explicit SourceInput(RetirementHandle<RetiredInput<T>> value) : value_(std::move(value)) {}
	const T *operator->() const noexcept {
		return std::visit(
			[](const auto &value) -> const T * {
				if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::shared_ptr<const T>>)
					return value.get();
				else
					return &value->value;
			},
			value_);
	}
	void reset() noexcept {
		std::visit([](auto &value) { value.reset(); }, value_);
	}

  private:
	std::variant<std::shared_ptr<const T>, RetirementHandle<RetiredInput<T>>> value_;
};
template <class T> SourceInput<T> own_input(T &&input, RetirementDomain &domain) {
	if (input.storage_account() != domain.storage_account())
		throw std::invalid_argument("owned input retirement account mismatch");
	return SourceInput<T>(domain.make<RetiredInput<T>>(std::move(input)));
}
} // namespace detent::distribution::detail
