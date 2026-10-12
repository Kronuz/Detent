#pragma once
#include "detent_local/storage_session.hh"
#include "paged_certificate.hh"
namespace detent::local::detail {
// Reads an explicitly named root and retains its shared lease through marking.
class PagedNamedRoot final : public kronuz::io::completion::Operation {
  public:
	PagedNamedRoot(distribution::RetirementFactory, std::shared_ptr<StorageSession>, StorageIdentity storage,
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
	const paged::Root &root() const;
	const Descriptor &descriptor() const;
	distribution::RetirementHandle<PagedDependencyCertificate> certificate() const;
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
