#pragma once
#include "detent_delivery/publication_sink.hh"
#include "detent_distribution/update_cache.hh"
#include <exception>

namespace detent::relay {
struct BundleOptions {
	distribution::Cursor configured;
	distribution::Identity incarnation;
	std::uint32_t namespaces = 0;
	std::size_t retained_edges = 64;
	distribution::Limits encoding;
};
struct BundleStatus {
	bool authenticated = false, ready = false, fenced = false;
	std::optional<distribution::Cursor> selected;
	std::optional<distribution::Cursor> snapshot_frontier;
	std::uint64_t staged = 0, gaps = 0, refusals = 0, snapshots = 0;
	std::uint64_t snapshot_started = 0, snapshot_discarded = 0, snapshot_discarded_logical_bytes = 0;
	std::size_t retained_edges = 0;
	std::exception_ptr error;
};
class BootstrapPlan final : public distribution::RetirementNode {
  public:
	std::array<distribution::RetirementHandle<distribution::EncodedUpdate>,
		distribution::UpdateCache::maximum_edges + 1> updates;
	distribution::Cursor head{};
	distribution::CacheMarker marker{};
	std::size_t count = 0, position = 0;
	std::uint64_t offered_window = 0;
	bool covers(const distribution::Cursor &base) const noexcept;
	void accepted(std::uint64_t window, std::optional<distribution::Cursor> base);
  private:
	bool retire_step() noexcept override;
};
struct BootstrapCapture {
	distribution::CacheDecision decision = distribution::CacheDecision::Busy;
	distribution::RetirementHandle<BootstrapPlan> plan;
};
// One finite configured scope. All methods run on the host's owner executor.
class Bundle final : public delivery::PublicationSink {
  public:
	Bundle(BundleOptions, distribution::RetirementDomain &);
	void authenticated() noexcept override;
	void begin_stage(const delivery::ReceivedUpdate &) override;
	bool stage_step(distribution::WorkBudget &) override;
	void bind_candidate(const local::Descriptor &, local::DirectoryIdentity) noexcept override;
	bool release_stage(distribution::WorkBudget &) noexcept override;
	void selected(const local::RecoveredGeneration &) override;
	void selected_equivalent(const local::RecoveredGeneration &,
							 const local::detail::StateEquivalenceProof &) override;
	void fence() noexcept override;
	distribution::CacheOffer offer(std::optional<distribution::Cursor>, bool force_snapshot = false) const;
	BootstrapCapture capture_bootstrap(std::optional<distribution::Cursor>);
	bool covers(distribution::CacheMarker, const distribution::Cursor &) const noexcept;
	bool snapshot_step(distribution::WorkBudget &);
	bool cleanup_step(distribution::WorkBudget &) noexcept;
	BundleStatus status() const noexcept;

  private:
	distribution::Identity next_identity();
	bool ready() const noexcept;
	void refused() noexcept;
	BundleOptions options_;
	distribution::RetirementDomain &domain_;
	distribution::UpdateCache cache_;
	std::optional<local::RecoveredGeneration> head_;
	std::optional<local::Descriptor> candidate_, snapshot_base_;
	std::optional<distribution::Cursor> requested_snapshot_;
	local::DirectoryIdentity candidate_directory_, snapshot_directory_;
	distribution::RetirementHandle<distribution::UpdateStage> stage_, snapshot_stage_;
	distribution::RetirementHandle<distribution::EncodedUpdate> token_;
	std::uint64_t sequence_ = 0;
	BundleStatus diagnostic_;
	bool stage_failed_ = false, snapshot_failed_ = false;
};
std::shared_ptr<Bundle> make_bundle(BundleOptions, distribution::RetirementDomain &);
} // namespace detent::relay
