#include "snapshot_source.hh"
#include "pinned_source.hh"

namespace detent::local::detail {
namespace d = distribution;
namespace {
class ImageSnapshotSource final : public d::SnapshotSource {
  public:
	ImageSnapshotSource(PinnedImageSource source, std::shared_ptr<d::StorageLedger> account)
		: source_(std::move(source)), account_(std::move(account)) {
		if (!account_ || source_.descriptor().profile != d::Profile{})
			throw std::invalid_argument("unsupported public snapshot source");
	}
	const d::Cursor &cursor() const noexcept override { return source_.descriptor().cursor; }
	std::shared_ptr<d::StorageLedger> storage_account() const noexcept override { return account_; }
	std::uint64_t namespace_count() const noexcept override { return source_.namespace_count(); }
	d::NamespaceRevision name_space(std::uint64_t ordinal) const override {
		if (ordinal >= namespace_count())
			throw std::out_of_range("snapshot image namespace ordinal");
		const auto row = source_.name_space(static_cast<std::uint32_t>(ordinal));
		return {row.name, row.revision};
	}
	void reset_records() noexcept override {
		record_ = 0;
		override_ = override_end_ = 0;
	}
	d::SnapshotRecordStep next_record() override {
		if (record_ == source_.record_count())
			return {{}, true};
		const auto row = source_.record(record_++);
		d::WireDefinition definition;
		override_ = override_end_ = 0;
		if (const auto *flag = std::get_if<SourceBoolean>(&row.definition)) {
			override_ = flag->override_start;
			if (override_ > source_.override_count() ||
				flag->override_count > source_.override_count() - override_)
				throw std::logic_error("snapshot image override range");
			override_end_ = override_ + flag->override_count;
			definition = d::WireBoolDef{flag->default_value, {0, flag->override_count}, flag->rollout_pct};
		} else {
			definition = std::get<IntDef>(row.definition);
		}
		return {d::Record{row.namespace_index, row.key, row.revision, definition}, false};
	}
	std::optional<d::OverrideRow> next_override() override {
		if (override_ == override_end_)
			return {};
		const auto row = source_.override_value(override_++);
		return d::OverrideRow{row.name, row.value};
	}

  private:
	PinnedImageSource source_;
	std::shared_ptr<d::StorageLedger> account_;
	std::uint32_t record_ = 0;
	std::uint64_t override_ = 0, override_end_ = 0;
};
} // namespace
std::shared_ptr<d::SnapshotSource> make_snapshot_source(RecoveredGeneration generation,
														std::shared_ptr<d::StorageLedger> account) {
	return make_snapshot_source(PinnedImageSource(std::move(generation)), std::move(account));
}
std::shared_ptr<d::SnapshotSource> make_snapshot_source(PinnedImageSource source,
														std::shared_ptr<d::StorageLedger> account) {
	if (!account)
		throw std::invalid_argument("snapshot source account missing");
	return std::allocate_shared<ImageSnapshotSource>(d::StorageAllocator<ImageSnapshotSource>(account),
													 std::move(source), account);
}
} // namespace detent::local::detail
