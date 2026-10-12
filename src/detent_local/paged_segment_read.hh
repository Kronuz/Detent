#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_local/paged_layout.hh"
#include "detent_local/storage_session.hh"
#include "segment_mapping.hh"
namespace detent::local::detail {
class SegmentProofError final : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
// Verifies a complete named pack; callers drive budgeted CPU work between IO originals.
class PagedSegmentRead final : public kronuz::io::completion::Operation {
  public:
	PagedSegmentRead(distribution::RetirementFactory, std::shared_ptr<StorageSession>,
					 std::shared_ptr<distribution::StorageLedger>, StorageIdentity storage,
					 StorageIdentity segment, std::uint64_t trusted_owner);
	~PagedSegmentRead();
	const kronuz::io::completion::Request &request() const override;
	void submitted() override;
	bool complete(kronuz::io::completion::Result) noexcept override;
	bool done() const noexcept override;
	bool in_flight() const noexcept override;
	kronuz::io::completion::IO &io() const noexcept override;
	bool needs_work() const noexcept;
	void step(ValidationBudget &);
	bool ready() const noexcept;
	const SegmentOwnership &ownership() const;
	std::shared_ptr<const distribution::OwnedBuffer> bytes() const;
	SegmentFileIdentity identity() const;
	std::shared_ptr<kronuz::io::completion::File> owner_file() const;
	std::shared_ptr<kronuz::io::completion::File> payload_file() const;
	std::exception_ptr error() const noexcept;
	std::exception_ptr closure_error() const noexcept;
	void close() noexcept;

  private:
	struct State;
	distribution::RetirementHandle<State> state_;
	std::shared_ptr<distribution::RetirementHandle<State>> pin_;
	kronuz::io::completion::Request request_;
	void prepare(kronuz::io::completion::Kind);
	void read_ownership();
	void cleanup();
};
} // namespace detent::local::detail
