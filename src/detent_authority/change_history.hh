#pragma once
#include "change_recorder.hh"
#include "detent_distribution/buffer.hh"
#include "state.hh"
namespace detent::authority {
struct HistoryOptions {
	std::size_t transactions = 4096, payload_capacity = 16 * 1024 * 1024, transaction_bytes = 256 * 1024;
};
// Internal canonical changed-only transaction. Its name is borrowed exclusively
// from its retained byte owner, never from an authority definition or root.
struct CachedChange {
	std::uint64_t revision = 0, overrides = 0, strings = 0;
	std::uint32_t mutations = 0;
	std::size_t namespace_offset = 0, namespace_length = 0;
	std::shared_ptr<const distribution::OwnedBuffer> body;
	std::string_view name_space() const noexcept {
		return {body->data() + namespace_offset, namespace_length};
	}
};
class ChangeHistory final : public AppliedChangeRecorder {
  public:
	ChangeHistory(std::shared_ptr<distribution::StorageLedger> account, HistoryOptions options = {});
	~ChangeHistory() override;
	void begin_batch(const Context &, std::uint64_t index, std::uint64_t base_revision) noexcept override;
	std::shared_ptr<AppliedChangeDraft> capture(const State &, const Command &,
												std::span<const unsigned char>) override;
	void stage(std::shared_ptr<AppliedChangeDraft>) noexcept override;
	void gap(std::uint64_t revision) noexcept override;
	void publish(const Context &, std::uint64_t index, std::uint64_t revision) noexcept override;
	void abandon_batch() noexcept override;
	void reset(const Context &, std::uint64_t revision) noexcept override;
	bool pending() const noexcept override;
	void collect(CommandWorkBudget &) noexcept override;
	bool covers(const Context &, std::uint64_t base, std::uint64_t target) const noexcept;
	std::shared_ptr<const CachedChange> find(std::uint64_t revision) const noexcept;
	std::size_t size() const noexcept;
	std::size_t payload_capacity() const noexcept;
	std::uint64_t coverage_floor() const noexcept;
	std::uint64_t through() const noexcept;

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::authority
