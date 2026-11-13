#pragma once
#include "history.hh"
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
	// Retained-state envelope: the operating defaults, independent of reductions to `consensus`.
	kronuz::consensus::WorkerLimits scheduling = [] {
		kronuz::consensus::WorkerLimits result;
		const kronuz::consensus::Limits defaults;
		result.retained = {defaults.command_bytes, defaults.log_bytes, defaults.log_entries,
						   defaults.rpc_bytes, defaults.rpc_entries};
		return result;
	}();
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
	// Proposes one administrative entry: the enabling entry, a namespace drop or a
	// layout policy. The enabling entry proposes the configured policy; both policies
	// take their log thresholds from this node's consensus limits.
	kronuz::consensus::SubmitResult propose_administrative(std::uint64_t request,
														   const AdministrativeCommand &command);
	kronuz::consensus::SubmitResult read(std::uint64_t request);
	void forget(std::uint64_t request);
	void service(kronuz::consensus::Tick tick);
	void service(kronuz::consensus::Tick tick, CommandWorkBudget &budget);
	std::vector<Completion> take_completions();
	kronuz::consensus::Actions take_network_actions();
	void attach_change_recorder(std::shared_ptr<AppliedChangeRecorder> recorder);
	// Keeps history (D6) in its own directory, before create() or recover(); `fetch`
	// reads a history file from a peer for repair and install.
	void attach_history(std::shared_ptr<kronuz::journal::IO> io, history::Fetch fetch = {});
	const history::History *history() const noexcept { return history_ ? &*history_ : nullptr; }
	const State &state() const noexcept { return state_; }
	// The state at the latest seal: where checkpoints are taken and seal files are cut.
	const std::optional<State> &sealed() const noexcept { return sealed_; }
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
		std::optional<AdministrativeApplication> administrative;
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
	std::optional<State> sealed_;
	std::optional<history::History> history_;
	std::shared_ptr<AppliedChangeRecorder> recorder_;
	std::uint64_t covered_ = UINT64_MAX;
	void cover() noexcept;
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
	void restored_history(bool install);
	const State *checkpoint_source() const noexcept;
};
} // namespace detent::authority
