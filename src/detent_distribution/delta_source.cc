#include "detent_distribution/delta_encoder.hh"
#include "detent_distribution/wire_encoder.hh"
#include <algorithm>
#include <limits>

namespace detent::distribution {
namespace m = messagepack;
namespace {
class ValidatedDeltaSource final : public DeltaSource {
  public:
	ValidatedDeltaSource(std::shared_ptr<const ValidatedDelta> delta, std::uint64_t namespaces,
						 std::shared_ptr<StorageLedger> account)
		: delta_(std::move(delta)), account_(std::move(account)), base_(delta_->base()),
		  target_(delta_->target()) {
		if (delta_->profile() != Profile{})
			throw std::invalid_argument("unsupported validated delta source");
		counts_.namespaces = namespaces;
		counts_.transactions = delta_->transactions().size();
	}
	const Cursor &base() const noexcept override { return base_; }
	const Cursor &target() const noexcept override { return target_; }
	std::shared_ptr<StorageLedger> storage_account() const noexcept override { return account_; }
	bool prepared() const noexcept override { return prepared_; }
	DeltaCounts counts() const noexcept override { return counts_; }
	void prepare(WorkBudget &budget) override {
		if (discarded_)
			throw std::logic_error("validated delta source discarded");
		while (!prepared_ && charge(budget))
			prepare_one();
	}
	void reset_fragments() noexcept override {
		transaction_ = row_ = override_ = credit_ = 0;
		phase_ = 0;
		overrides_ = {};
		scratch_.clear();
	}
	DeltaFragmentStep next_fragment(WorkBudget &budget) override {
		if (!prepared_ || discarded_)
			throw std::logic_error("validated delta source not ready");
		if (!charge(budget))
			return {};
		scratch_.clear();
		if (transaction_ == counts_.transactions)
			return {{}, true};
		const auto &tx = delta_->transactions()[transaction_];
		if (phase_ == 0) {
			m::array(scratch_, 3);
			m::unsigned_integer(scratch_, tx.revision);
			m::array(scratch_, tx.touched.count);
			phase_ = 1;
		} else if (phase_ == 1) {
			if (row_ < tx.touched.count) {
				const auto &row = delta_->touched(tx.touched.start + row_++);
				m::array(scratch_, 2);
				m::string(scratch_, row.name);
				m::unsigned_integer(scratch_, row.revision);
			} else {
				m::array(scratch_, tx.mutations.count);
				row_ = 0;
				phase_ = 2;
			}
		} else if (phase_ == 3) {
			if (override_ < overrides_.size()) {
				const auto &row = overrides_[override_++];
				detail::wire::override_row(scratch_, row.first, row.second);
			} else {
				overrides_ = {};
				phase_ = 2;
			}
		} else if (row_ < tx.mutations.count) {
			const auto &row = delta_->mutation(tx.mutations.start + row_++);
			m::array(scratch_, 4);
			m::unsigned_integer(scratch_, row.namespace_index);
			m::string(scratch_, row.key);
			m::unsigned_integer(scratch_, row.value ? 0 : 1);
			if (row.value) {
				detail::wire::definition(scratch_, *row.value);
				if (const auto *flag = std::get_if<WireBoolDef>(&*row.value)) {
					overrides_ = delta_->overrides(*flag);
					override_ = 0;
					phase_ = 3;
				}
			} else
				scratch_.push_back('\xc0');
		} else {
			++transaction_;
			row_ = phase_ = 0;
		}
		return {scratch_, false};
	}
	bool discard(WorkBudget &budget) noexcept override {
		discarded_ = true;
		reset_fragments();
		if (delta_ && delta_.use_count() == 1) {
			if (!budget.tokens)
				return false;
			if (!disposal_)
				disposal_ = delta_->storage_capacity();
			const auto n = std::min(disposal_, budget.bytes);
			disposal_ -= n;
			budget.bytes -= n;
			if (disposal_)
				return false;
			--budget.tokens;
		}
		delta_.reset();
		return true;
	}

  private:
	bool charge(WorkBudget &budget) {
		if (!budget.tokens || !budget.bytes)
			return false;
		if (!credit_)
			credit_ = 512;
		const auto n = std::min(credit_, budget.bytes);
		credit_ -= n;
		budget.bytes -= n;
		if (credit_)
			return false;
		--budget.tokens;
		return true;
	}
	static void add(std::uint64_t &value, std::uint64_t increment) {
		if (increment > std::numeric_limits<std::uint64_t>::max() - value)
			throw DeltaRepresentationLimit("validated delta aggregate overflow");
		value += increment;
	}
	void prepare_one() {
		if (transaction_ == counts_.transactions) {
			prepared_ = true;
			return;
		}
		const auto &tx = delta_->transactions()[transaction_];
		if (phase_ == 0) {
			if (row_ < tx.touched.count) {
				add(counts_.strings, delta_->touched(tx.touched.start + row_++).name.size());
			} else {
				row_ = 0;
				phase_ = 1;
			}
		} else if (phase_ == 2) {
			if (override_ < overrides_.size()) {
				const auto &row = overrides_[override_++];
				add(counts_.strings, row.first.size());
				add(counts_.overrides, 1);
				add(definition_bytes_,
					9 + row.first.size() + (std::holds_alternative<bool>(row.second) ? 1 : 8));
			} else {
				counts_.maximum_definition_bytes =
					std::max(counts_.maximum_definition_bytes, definition_bytes_);
				overrides_ = {};
				phase_ = 1;
			}
		} else if (row_ < tx.mutations.count) {
			const auto &row = delta_->mutation(tx.mutations.start + row_++);
			if (row.namespace_index >= counts_.namespaces)
				throw std::invalid_argument("validated delta source namespace mismatch");
			add(counts_.mutations, 1);
			add(counts_.strings, row.key.size());
			if (row.value) {
				if (const auto *flag = std::get_if<WireBoolDef>(&*row.value)) {
					overrides_ = delta_->overrides(*flag);
					counts_.maximum_overrides = std::max(counts_.maximum_overrides, overrides_.size());
					definition_bytes_ = flag->rollout_pct ? 19 : 11;
					override_ = 0;
					phase_ = 2;
				} else
					counts_.maximum_definition_bytes =
						std::max<std::uint64_t>(counts_.maximum_definition_bytes, 9);
			}
		} else {
			++transaction_;
			row_ = phase_ = 0;
		}
	}
	std::shared_ptr<const ValidatedDelta> delta_;
	std::shared_ptr<StorageLedger> account_;
	Cursor base_, target_;
	DeltaCounts counts_;
	OverrideRange overrides_;
	m::InlineBytes<512> scratch_;
	std::uint64_t transaction_ = 0, row_ = 0, override_ = 0, definition_bytes_ = 0;
	std::size_t credit_ = 0, disposal_ = 0;
	unsigned phase_ = 0;
	bool prepared_ = false, discarded_ = false;
};
} // namespace
std::shared_ptr<DeltaSource> make_delta_source(std::shared_ptr<const ValidatedDelta> delta,
											   std::uint64_t namespaces,
											   std::shared_ptr<StorageLedger> output_account) {
	if (!delta)
		throw std::invalid_argument("validated delta source missing");
	if (!output_account)
		output_account = delta->storage_account();
	return std::allocate_shared<ValidatedDeltaSource>(StorageAllocator<ValidatedDeltaSource>(output_account),
													  delta, namespaces, output_account);
}
} // namespace detent::distribution
