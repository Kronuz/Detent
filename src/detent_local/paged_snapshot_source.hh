#pragma once
#include "paged_cursor.hh"
#include "snapshot_source.hh"
#include <vector>
namespace detent::local::detail {
class PagedSnapshotSource final : public distribution::SnapshotSource {
  public:
	PagedSnapshotSource(RecoveredGeneration, std::shared_ptr<distribution::StorageLedger>,
						const distribution::Cursor &identity);
	const distribution::Cursor &cursor() const noexcept override { return cursor_; }
	std::shared_ptr<distribution::StorageLedger> storage_account() const noexcept override {
		return account_;
	}
	std::uint64_t namespace_count() const noexcept override { return expected_namespaces_; }
	distribution::NamespaceRevision name_space(std::uint64_t) const override;
	bool prepare(distribution::WorkBudget &) override;
	void reset_records() noexcept override;
	distribution::SnapshotRecordStep next_record() override;
	std::optional<distribution::OverrideRow> next_override() override;

  private:
	struct Namespace {
		std::array<char, 198> text{};
		std::uint16_t length = 0;
		std::uint64_t revision = 0;
		std::string_view name() const { return {text.data(), length}; }
	};
	Descriptor descriptor_;
	distribution::Cursor cursor_;
	std::shared_ptr<distribution::StorageLedger> account_;
	PagedCursor namespaces_cursor_, records_cursor_;
	std::vector<Namespace, distribution::StorageAllocator<Namespace>> namespaces_;
	std::array<char, 200> record_key_{};
	std::span<const char> overrides_;
	std::size_t override_offset_ = 0, preparation_charge_ = 256;
	std::uint32_t expected_namespaces_ = 0, overrides_remaining_ = 0;
	bool prepared_ = false;
};
} // namespace detent::local::detail
