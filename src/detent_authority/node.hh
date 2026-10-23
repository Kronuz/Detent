#pragma once
#include "state.hh"
#include <consensus/snapshot_channel.h>
#include <consensus/worker.h>
#include <map>
#include <metrics.hh>
#include <sha256.h>

namespace detent::authority {
class PreparedProposal;
struct NodeOptions {
	Context context;
	Limits application;
	kronuz::consensus::FixedConfiguration configuration;
	kronuz::consensus::Limits consensus;
	kronuz::consensus::Timing timing;
	kronuz::consensus::WorkerLimits scheduling;
	kronuz::journal::AdmissionLimits storage{
		{1024ull * 1024 * 1024, 65536}, {8ull * 1024 * 1024, 3}, {512ull * 1024 * 1024, 4}, 64, 3};
	std::size_t waiters = 1024;
};
void validate_node_options(const NodeOptions &);
struct Completion {
	kronuz::consensus::RequestId request;
	std::uint64_t index;
	std::vector<Result> results;
	std::optional<kronuz::consensus::RejectReason> rejection;
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
	kronuz::consensus::SubmitResult propose(std::uint64_t request, const std::vector<Command> &commands);
	kronuz::consensus::SubmitResult propose_prepared(std::uint64_t request, PreparedProposal &proposal);
	kronuz::consensus::SubmitResult read(std::uint64_t request);
	void forget(std::uint64_t request);
	void service(kronuz::consensus::Tick tick);
	void service(kronuz::consensus::Tick tick, CommandWorkBudget &budget);
	std::vector<Completion> take_completions();
	kronuz::consensus::Actions take_network_actions();
	void attach_change_recorder(std::shared_ptr<AppliedChangeRecorder> recorder);
	const State &state() const noexcept { return state_; }
	kronuz::consensus::Worker &worker() noexcept { return worker_; }
	kronuz::consensus::SnapshotChannel &channel() noexcept { return channel_; }
	bool checkpoint();
	bool checkpoint_busy() const noexcept { return capture_.has_value(); }
	bool application_work_pending() const noexcept {
		return application_.has_value() || retired_.has_value() || state_.reclamation_pending() ||
			   (recorder_ && recorder_->pending());
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
		kronuz::consensus::Committed range;
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
		kronuz::consensus::CheckpointId id;
		State state;
		ImageEncoder encoder;
		std::string chunk;
		bool final = false;
	};
	struct Candidate {
		kronuz::consensus::SnapshotId id;
		ImageDecoder decoder;
		std::size_t offset = 0;
		std::optional<kronuz::consensus::Token> final_chunk;
		std::optional<State> state;
	};
	std::shared_ptr<kronuz::journal::IO> io_;
	NodeOptions options_;
	State state_;
	std::shared_ptr<AppliedChangeRecorder> recorder_;
	kronuz::consensus::Worker worker_;
	kronuz::consensus::SnapshotChannel channel_;
	std::map<std::uint64_t, Pending> pending_;
	std::vector<Completion> completions_;
	kronuz::consensus::Actions network_;
	std::optional<Application> application_;
	std::optional<Capture> capture_;
	std::optional<Candidate> candidate_;
	std::optional<State> retired_;
	std::uint64_t checkpoint_request_ = std::uint64_t{1} << 63;
	CheckpointPolicy checkpoint_policy_;
	bool closing_ = false, state_retired_ = false;
	std::optional<std::uint64_t> checkpoint_attempt_;
	void refresh_checkpoint_due(kronuz::consensus::Tick tick);
	void service_checkpoint_due(kronuz::consensus::Tick tick);
	void drain();
	void advance_application(CommandWorkBudget &budget);
	void discard_candidate();
};
} // namespace detent::authority
