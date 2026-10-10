#pragma once
#include "buffer.hh"
#include "protocol.hh"

namespace detent::distribution {
class DeltaRepresentationLimit final : public std::length_error {
  public:
	using std::length_error::length_error;
};
struct DeltaCounts {
	std::uint64_t namespaces = 0, transactions = 0, mutations = 0, overrides = 0, strings = 0;
	std::uint64_t maximum_definition_bytes = 0, maximum_overrides = 0;
};
struct DeltaFragmentStep {
	std::string_view bytes;
	bool done = false;
};
// Frozen, owner-local canonical transaction fragments. Preparation and fragment
// iteration consume supplied work; borrowed bytes live until the next call.
class DeltaSource {
  public:
	virtual ~DeltaSource() = default;
	virtual const Cursor &base() const noexcept = 0;
	virtual const Cursor &target() const noexcept = 0;
	virtual std::shared_ptr<StorageLedger> storage_account() const noexcept = 0;
	virtual void prepare(WorkBudget &) = 0;
	virtual bool prepared() const noexcept = 0;
	virtual DeltaCounts counts() const noexcept = 0;
	virtual void reset_fragments() noexcept = 0;
	virtual DeltaFragmentStep next_fragment(WorkBudget &) = 0;
	virtual bool discard(WorkBudget &) noexcept = 0;
};
std::shared_ptr<DeltaSource> make_delta_source(std::shared_ptr<const ValidatedDelta>,
											   std::uint64_t namespaces);
class DeltaEncoder {
  public:
	explicit DeltaEncoder(std::shared_ptr<DeltaSource>, Limits = {});
	~DeltaEncoder();
	void step(WorkBudget &);
	bool counted() const noexcept;
	std::uint64_t encoded_length() const;
	UpdateKind kind() const;
	bool done() const noexcept;
	bool chunk_ready() const noexcept;
	std::shared_ptr<const OwnedBuffer> take_chunk();
	bool discard(WorkBudget &) noexcept;

  private:
	struct Impl;
	StorageOwner<Impl> impl_;
};
} // namespace detent::distribution
