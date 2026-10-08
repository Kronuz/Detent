#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_distribution/protocol.hh"
#include "export_capture.hh"
namespace detent::authority {
// Owner-executor count/emission over one frozen public projection. Only sealed
// independently owned chunks may cross to transport; never the capture.
class SnapshotExportEncoder {
  public:
	explicit SnapshotExportEncoder(std::shared_ptr<ExportCapture> capture, distribution::Limits limits = {});
	~SnapshotExportEncoder();
	SnapshotExportEncoder(SnapshotExportEncoder &&) noexcept;
	SnapshotExportEncoder(const SnapshotExportEncoder &) = delete;
	void step(distribution::WorkBudget &budget);
	bool counted() const noexcept;
	std::uint64_t encoded_length() const;
	bool done() const noexcept;
	bool chunk_ready() const noexcept;
	std::shared_ptr<const distribution::OwnedBuffer> take_chunk();
	// Drops borrowed roots; the coordinator then retires/drains its capture slot.
	void discard() noexcept;

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::authority
