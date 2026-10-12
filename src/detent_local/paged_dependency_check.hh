#pragma once
#include "detent_local/storage_session.hh"
#include "paged_publication_artifacts.hh"
namespace detent::local::detail {
// Read-only, serial named checks over dependencies retained by an actual-base plan.
class PagedDependencyCheck final : public kronuz::io::completion::Operation {
  public:
	PagedDependencyCheck(distribution::RetirementFactory, std::shared_ptr<StorageSession>,
						 distribution::RetirementHandle<PagedPublicationArtifacts>,
						 std::uint64_t trusted_owner);
	~PagedDependencyCheck();
	const kronuz::io::completion::Request &request() const override;
	void submitted() override;
	bool complete(kronuz::io::completion::Result) noexcept override;
	bool done() const noexcept override;
	bool in_flight() const noexcept override;
	kronuz::io::completion::IO &io() const noexcept override;
	void cancel() noexcept;
	std::exception_ptr error() const noexcept;

  private:
	struct State;
	distribution::RetirementHandle<State> data_;
	std::shared_ptr<distribution::RetirementHandle<State>> pin_;
	kronuz::io::completion::Request request_;
	void prepare(kronuz::io::completion::Kind);
	void prepare_reader();
	void next();
};
} // namespace detent::local::detail
