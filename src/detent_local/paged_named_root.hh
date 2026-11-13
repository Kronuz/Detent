#pragma once
#include "detent_local/storage_session.hh"
#include "paged_certificate.hh"
#include "live_record.hh"
namespace detent::local::detail {
// Reads an explicitly named root and retains its shared lease through marking.
class PagedNamedRoot final : public kronuz::io::completion::Operation {
  public:
	PagedNamedRoot(distribution::RetirementFactory, std::shared_ptr<StorageSession>, StorageIdentity lease_storage,
				   GenerationId generation, std::uint64_t trusted_owner,
				   distribution::RetirementHandle<PagedDependencyCertificate> cached = {});
	~PagedNamedRoot();
	const kronuz::io::completion::Request &request() const override;
	void submitted() override;
	bool complete(kronuz::io::completion::Result) noexcept override;
	bool done() const noexcept override;
	bool in_flight() const noexcept override;
	kronuz::io::completion::IO &io() const noexcept override;
	bool ready() const noexcept;
	bool paged() const;
	bool legacy_anchor() const;
	const paged::Root &root() const;
	// The generation as its lease and root hold it; neither holds the build ordinal, which is 0 here.
	const Descriptor &descriptor() const;
	DirectoryIdentity directory_identity() const;
	distribution::RetirementHandle<PagedDependencyCertificate> certificate() const;
	void read_live(std::shared_ptr<distribution::StorageLedger>, std::size_t maximum_segments);
	std::span<const LiveRow> live_dependencies() const;
	std::exception_ptr error() const noexcept;
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
