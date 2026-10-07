#pragma once
#include "state.hh"
#include <consensus/snapshot_channel.h>
#include <consensus/worker.h>
#include <map>
#include <metrics.h>
#include <sha256.h>

namespace detent::authority {
class PreparedProposal;
struct NodeOptions {
	Context context;
	Limits application;
	cluster::consensus::FixedConfiguration configuration;
	cluster::consensus::Limits consensus;
	cluster::consensus::Timing timing;
	cluster::consensus::WorkerLimits scheduling;
	kronuz::journal::AdmissionLimits storage{
		{1024ull * 1024 * 1024, 65536}, {8ull * 1024 * 1024, 3}, {512ull * 1024 * 1024, 4}, 64, 3};
	std::size_t waiters = 1024;
};
struct Completion {
	cluster::consensus::RequestId request;
	std::uint64_t index;
	std::vector<Result> results;
	std::optional<cluster::consensus::RejectReason> rejection;
	bool read = false;
};
// All operations and original IO completions belong to one owner executor.
// Production transport must marshal messages onto that executor.
class Node {
  public:
	Node(std::unique_ptr<kronuz::journal::IO> io, NodeOptions options);
	void create(Identity storage_identity);
	void recover();
	void start();
	void close() noexcept;
	void retirement_step(CommandWorkBudget &budget);
	bool drained() const noexcept;
	cluster::consensus::SubmitResult propose(std::uint64_t request, const std::vector<Command> &commands);
	cluster::consensus::SubmitResult propose_prepared(std::uint64_t request, PreparedProposal &proposal);
	cluster::consensus::SubmitResult read(std::uint64_t request);
	void forget(std::uint64_t request);
	void service(cluster::consensus::Tick tick);
	void service(cluster::consensus::Tick tick, CommandWorkBudget &budget);
	std::vector<Completion> take_completions();
	cluster::consensus::Actions take_network_actions();
	const State &state() const noexcept { return state_; }
	cluster::consensus::Worker &worker() noexcept { return worker_; }
	cluster::consensus::SnapshotChannel &channel() noexcept { return channel_; }
	bool checkpoint();
	bool checkpoint_busy() const noexcept { return capture_.has_value(); }
	bool application_work_pending() const noexcept {
		return application_.has_value() || retired_.has_value() || state_.reclamation_pending();
	}
	struct CheckpointPolicy {
		bool due = false;
		std::uint64_t due_since = 0, captured_boundary = 0, published_boundary = 0;
		kronuz::metrics::Counter attempts, reservation_unavailable;
	};
	const CheckpointPolicy &checkpoint_policy() const noexcept { return checkpoint_policy_; }

  private:
	struct Pending {
		std::uint64_t index = 0;
		std::uint64_t term = 0;
		Digest identity{};
		bool read = false;
	};
	struct Application {
		cluster::consensus::Committed range;
		std::size_t cursor = 0;
		std::uint64_t through = 0;
		std::optional<BatchDecoder> decoder;
		std::optional<BatchApplication> candidate;
		std::optional<CommandDecoder> command;
		SHA256 hash;
		std::size_t hashed = 0;
		std::optional<Digest> identity;
		bool failed = false;
	};
	struct Capture {
		cluster::consensus::CheckpointId id;
		State state;
		ImageEncoder encoder;
		std::string chunk;
		bool final = false;
	};
	struct Candidate {
		cluster::consensus::SnapshotId id;
		ImageDecoder decoder;
		std::size_t offset = 0;
		std::optional<cluster::consensus::Token> final_chunk;
		std::optional<State> state;
	};
	std::shared_ptr<kronuz::journal::IO> io_;
	NodeOptions options_;
	State state_;
	cluster::consensus::Worker worker_;
	cluster::consensus::SnapshotChannel channel_;
	std::map<std::uint64_t, Pending> pending_;
	std::vector<Completion> completions_;
	cluster::consensus::Actions network_;
	std::optional<Application> application_;
	std::optional<Capture> capture_;
	std::optional<Candidate> candidate_;
	std::optional<State> retired_;
	std::uint64_t checkpoint_request_ = std::uint64_t{1} << 63;
	CheckpointPolicy checkpoint_policy_;
	bool closing_ = false, state_retired_ = false;
	std::optional<std::uint64_t> checkpoint_attempt_;
	void refresh_checkpoint_due(cluster::consensus::Tick tick);
	void service_checkpoint_due(cluster::consensus::Tick tick);
	void drain();
	void advance_application(CommandWorkBudget &budget);
	void discard_candidate();
};
} // namespace detent::authority
