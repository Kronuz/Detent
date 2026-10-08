#pragma once
#include "disk_ledger.hh"
#include "storage_session.hh"

namespace detent::local {
namespace inventory_completion = kronuz::io::completion;
enum class InventoryStatus { Scanning, Ready, Cancelled, Failed };
enum class InventoryIssue { Foreign, Unsafe, Directory };
struct InventorySample {
	std::array<char, 256> name{};
	std::size_t size = 0;
	InventoryIssue issue = InventoryIssue::Foreign;
};
struct InventoryReport {
	DiskUsage usage;
	std::uint64_t inspected = 0, quarantined = 0, canonical_payloads = 0;
	std::array<InventorySample, 8> samples{};
	std::size_t sample_count = 0;
};
// Exact quiescent named-space census, not ownership or deletion authority.
// The coordinator pauses mutations and settles preceding originals first.
class InventoryOperation final : public inventory_completion::Operation {
  public:
	InventoryOperation(std::shared_ptr<StorageSession> session, std::uint64_t work_limit,
					   std::uint64_t trusted_owner);
	~InventoryOperation() override;
	const inventory_completion::Request &request() const override;
	void submitted() override;
	bool complete(inventory_completion::Result result) noexcept override;
	bool done() const noexcept override;
	bool in_flight() const noexcept override;
	inventory_completion::IO &io() const noexcept override;
	void cancel() noexcept;
	InventoryStatus status() const noexcept;
	const InventoryReport &report() const;
	std::exception_ptr error() const noexcept;

  private:
	struct State;
	std::shared_ptr<State> state_;
	inventory_completion::Request request_;
	void next(inventory_completion::Kind kind);
	void finish_or_close();
	void observe(const inventory_completion::EntryInspection &entry);
};
std::shared_ptr<InventoryOperation> make_inventory_operation(std::shared_ptr<StorageSession> session,
															 std::uint64_t work_limit,
															 std::uint64_t trusted_owner);
} // namespace detent::local
