#include "export_encoder.hh"
namespace detent::authority {
namespace d = distribution;
namespace {
class AuthoritySnapshotSource final : public d::SnapshotSource {
  public:
	explicit AuthoritySnapshotSource(std::shared_ptr<ExportCapture> capture) : capture_(std::move(capture)) {}
	const d::Cursor &cursor() const noexcept override { return capture_->cursor(); }
	std::shared_ptr<d::StorageLedger> storage_account() const noexcept override {
		return capture_->storage_account();
	}
	std::uint64_t namespace_count() const noexcept override { return capture_->scope().size(); }
	d::NamespaceRevision name_space(std::uint64_t ordinal) const override {
		return {capture_->scope().name(ordinal), capture_->namespace_revision(ordinal)};
	}
	void reset_records() noexcept override {
		group_.reset();
		definition_ = nullptr;
		records_.reset();
		namespace_ = 0;
	}
	d::SnapshotRecordStep next_record() override {
		group_.reset();
		definition_ = nullptr;
		if (!records_) {
			if (namespace_ == namespace_count())
				return {{}, true};
			records_.emplace(capture_->records(namespace_++));
			return {};
		}
		auto row = records_->next();
		if (!row) {
			records_.reset();
			return {};
		}
		definition_ = row->definition;
		group_ = definition_->overrides.begin();
		d::WireDefinition definition{definition_->default_value,
									 {0, static_cast<std::uint64_t>(definition_->overrides.size())},
									 definition_->rollout_pct, definition_->rollout_value};
		return {d::Record{row->namespace_index, row->key, row->revision, definition}, false};
	}
	std::optional<d::OverrideRow> next_override() override {
		if (!definition_ || !group_ || *group_ == definition_->overrides.end())
			return {};
		const auto &row = **group_;
		++*group_;
		return d::OverrideRow{row.first, row.second};
	}

  private:
	std::shared_ptr<ExportCapture> capture_;
	std::optional<ExportCapture::RecordCursor> records_;
	std::optional<decltype(KnobDef::overrides)::const_iterator> group_;
	const KnobDef *definition_ = nullptr;
	std::uint64_t namespace_ = 0;
};
std::shared_ptr<d::SnapshotSource> source(std::shared_ptr<ExportCapture> capture) {
	if (!capture)
		throw std::invalid_argument("snapshot export capture missing");
	return std::allocate_shared<AuthoritySnapshotSource>(
		d::StorageAllocator<AuthoritySnapshotSource>(capture->storage_account()), capture);
}
} // namespace
SnapshotExportEncoder::SnapshotExportEncoder(std::shared_ptr<ExportCapture> capture, d::Limits limits)
	: encoder_(source(std::move(capture)), std::move(limits)) {}
SnapshotExportEncoder::~SnapshotExportEncoder() = default;
SnapshotExportEncoder::SnapshotExportEncoder(SnapshotExportEncoder &&) noexcept = default;
void SnapshotExportEncoder::step(d::WorkBudget &budget) { encoder_.step(budget); }
std::uint64_t SnapshotExportEncoder::encoded_length() const { return encoder_.encoded_length(); }
bool SnapshotExportEncoder::counted() const noexcept { return encoder_.counted(); }
bool SnapshotExportEncoder::done() const noexcept { return encoder_.done(); }
bool SnapshotExportEncoder::chunk_ready() const noexcept { return encoder_.chunk_ready(); }
std::shared_ptr<const d::OwnedBuffer> SnapshotExportEncoder::take_chunk() { return encoder_.take_chunk(); }
void SnapshotExportEncoder::discard() noexcept { encoder_.discard(); }
} // namespace detent::authority
