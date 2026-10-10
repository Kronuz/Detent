#include "detent_distribution/snapshot_encoder.hh"
#include "source_input.hh"

namespace detent::distribution {
namespace {
class ValidatedSnapshotSource final : public SnapshotSource {
  public:
	ValidatedSnapshotSource(detail::SourceInput<ValidatedSnapshot> snapshot,
							std::shared_ptr<StorageLedger> account)
		: snapshot_(std::move(snapshot)), account_(std::move(account)) {
		if (snapshot_->profile() != Profile{})
			throw std::invalid_argument("unsupported validated snapshot source");
	}
	const Cursor &cursor() const noexcept override { return snapshot_->cursor(); }
	std::shared_ptr<StorageLedger> storage_account() const noexcept override { return account_; }
	std::uint64_t namespace_count() const noexcept override { return snapshot_->namespaces().size(); }
	NamespaceRevision name_space(std::uint64_t ordinal) const override {
		if (ordinal >= namespace_count())
			throw std::out_of_range("validated snapshot namespace ordinal");
		return snapshot_->namespaces()[ordinal];
	}
	void reset_records() noexcept override {
		record_ = override_ = 0;
		overrides_ = {};
	}
	SnapshotRecordStep next_record() override {
		if (record_ == snapshot_->records().size())
			return {{}, true};
		const auto &row = snapshot_->records()[record_++];
		override_ = 0;
		overrides_ = {};
		if (const auto *flag = std::get_if<WireBoolDef>(&row.definition))
			overrides_ = snapshot_->overrides(*flag);
		return {row, false};
	}
	std::optional<OverrideRow> next_override() override {
		if (override_ == overrides_.size())
			return {};
		return overrides_[override_++];
	}

	bool discard(WorkBudget &) noexcept override {
		overrides_ = {};
		snapshot_.reset();
		return true;
	}

  private:
	detail::SourceInput<ValidatedSnapshot> snapshot_;
	std::shared_ptr<StorageLedger> account_;
	OverrideRange overrides_;
	std::uint64_t record_ = 0, override_ = 0;
};
} // namespace
std::shared_ptr<SnapshotSource> make_snapshot_source(std::shared_ptr<const ValidatedSnapshot> snapshot,
													 std::shared_ptr<StorageLedger> output_account) {
	if (!snapshot)
		throw std::invalid_argument("validated snapshot source missing");
	if (!output_account)
		output_account = snapshot->storage_account();
	return std::allocate_shared<ValidatedSnapshotSource>(
		StorageAllocator<ValidatedSnapshotSource>(output_account),
		detail::SourceInput<ValidatedSnapshot>(std::move(snapshot)), output_account);
}
std::shared_ptr<SnapshotSource> make_snapshot_source(ValidatedSnapshot &&snapshot,
													 RetirementDomain &input_domain,
													 std::shared_ptr<StorageLedger> output_account) {
	if (!output_account)
		output_account = snapshot.storage_account();
	auto input = detail::own_input(std::move(snapshot), input_domain);
	return std::allocate_shared<ValidatedSnapshotSource>(
		StorageAllocator<ValidatedSnapshotSource>(output_account), std::move(input), output_account);
}
} // namespace detent::distribution
