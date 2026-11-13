#pragma once

#include "detent/model.hh"
#include "change_recorder.hh"
#include "persistent_map.hh"
#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace detent::authority {
namespace detail {
template <class Value> class DeferredValues;
}
using Revision = std::uint64_t;
using Identity = std::array<char, 16>;
struct Context {
	Identity cluster{}, group{}, epoch{};
	bool operator==(const Context &) const = default;
};
// History's layout policy (namespace-delivery D6): every value an integer, so
// every voter seals, rolls up and cuts at the same entries. α = alpha_num / alpha_den.
struct LayoutPolicy {
	std::uint64_t seal_transactions = 2048, seal_bytes = 8 * 1024 * 1024; // positioned changes per seal
	std::uint64_t period = 64;									   // seals between a namespace's roll-ups
	std::uint64_t carry_bytes = 4096, carry_count = 2;			   // a small roll-up stays a section
	std::uint64_t ratio = 4, alpha_num = 1, alpha_den = 8, cap = 2; // squash ratio, α, disk cap (in α)
	// The group's consensus log limits; a seal also fires every eighth of either.
	std::uint64_t log_entries = 65536, log_bytes = 64 * 1024 * 1024;
	bool operator==(const LayoutPolicy &) const = default;
};
void validate(const LayoutPolicy &policy);
// The policy's canonical bytes: eleven little-endian 64-bit words, in field order.
std::string encode_policy(const LayoutPolicy &policy);
LayoutPolicy decode_policy(std::string_view bytes);
struct Limits {
	std::size_t keys = 500000, namespaces = 4096, producers = 1024;
	std::size_t mutations = 128, request_bytes = 256 * 1024, state_bytes = 128 * 1024 * 1024,
				image_bytes = 256 * 1024 * 1024;
	std::size_t definition_bytes = 16 * 1024, overrides = 256, results = 256;
	// The policy an enabling entry proposes. Not in the semantic profile: the
	// policy in force is replicated through the log (enabling and SetLayout).
	LayoutPolicy layout{};
	bool operator==(const Limits &) const = default;
};
struct Mutation {
	std::string key;
	std::optional<KnobDef> value;
	std::optional<Revision> expected; // 0 means absent; requires absence_revision.
	std::optional<Revision> absence_revision;
	bool operator==(const Mutation &) const = default;
};
enum class Kind : std::uint8_t { Open = 1, Close = 2, Transaction = 3 };
struct Command {
	Context context;
	Kind kind = Kind::Transaction;
	std::string producer;
	std::uint64_t generation = 0, sequence = 0;
	Identity nonce{}; // Open identity; expected generation fences stale opens.
	std::string name_space;
	std::optional<Revision> expected_namespace;
	std::vector<Mutation> mutations;
};
enum class Status : std::uint8_t {
	Applied = 1,
	NoChange,
	Conflict,
	UnknownSession,
	FencedSession,
	SequenceGap,
	ResultUnavailable,
	IdentityConflict,
	Capacity,
	Exhausted
};
struct Result {
	Status status = Status::Applied;
	Revision revision = 0, namespace_revision = 0;
	std::uint64_t generation = 0, sequence = 0;
	bool operator==(const Result &) const = default;
};
struct Record {
	Revision revision;
	std::shared_ptr<const KnobDef> value;
};
// A namespace's directory entry. Before the enabling entry `position` is the
// group revision of its last change; after it, the namespace's own counter.
struct Namespace {
	Revision position = 0;
	Identity incarnation{};		   // Zero until positions are enabled.
	std::uint64_t directory = 0;   // Directory position of its latest creation or drop.
	std::uint64_t bytes = 0;	   // Live size: the canonical footprint of its records.
	Revision origin = 0;		   // The position before this incarnation's first change.
	bool live = true;			   // False for a dropped namespace's tombstone.
	bool operator==(const Namespace &) const = default;
};
// A consumer's place in one namespace (design W1).
struct NamespaceCursor {
	Identity cluster{}, incarnation{};
	std::uint64_t position = 0;
	bool operator==(const NamespaceCursor &) const = default;
};
// What a cursor needs: nothing, the changes after it, a wait for this replica
// to catch up, a full copy (another cluster or incarnation), or nothing to follow.
enum class CursorAnswer : std::uint8_t { Current = 1, Behind, Ahead, FullCopy, Absent };
// Replicated entries that are not producer transactions; each applies at one log index.
// SetLayout is the "set limits" entry: its policy takes effect at the next seal.
enum class AdministrativeKind : std::uint8_t { EnablePositions = 1, DropNamespace = 2, SetLayout = 3 };
struct AdministrativeCommand {
	Context context;
	AdministrativeKind kind = AdministrativeKind::EnablePositions;
	Identity nonce{};		// EnablePositions: the seed of every incarnation.
	std::string name_space; // DropNamespace: the name, and the incarnation it drops,
	Identity incarnation{}; // so a retried drop never removes a re-creation.
	LayoutPolicy policy{};	// EnablePositions and SetLayout: the layout policy.
	bool operator==(const AdministrativeCommand &) const = default;
};
// The seal trigger's counters since the last seal, and the seal ordinal: the
// count of seals since the enabling entry, which is seal 0.
struct SealCounters {
	std::uint64_t ordinal = 0, index = 0;		  // The last seal's ordinal and log index.
	std::uint64_t transactions = 0, bytes = 0;	  // Positioned changes and their canonical bytes.
	std::uint64_t entries = 0, log_bytes = 0;	  // Log entries and their payload bytes.
	bool forced = false;						  // The enabling entry seals where it applies.
	bool operator==(const SealCounters &) const = default;
};
using Digest = std::array<unsigned char, 32>;
// One budget is shared across all phases of an owning executor turn.
struct CommandWorkBudget {
	std::size_t bytes = 8192, records = 16, updates = 8, receipts = 32;
};
namespace detail {
std::size_t collect_definition(DeferredValues<KnobDef> &arena, CommandWorkBudget &budget) noexcept;
}
class CommandDecoder;
class CommandApplication;
class EncodedCommand;
// Borrows an immutable command until completion or bounded discard. Mutation
// references are ordered without copying definitions; overrides emit one row
// at a time. The source owner must outlive the cursor.
class CommandEncoder {
  public:
	CommandEncoder(const Command &command, Limits limits = {});
	~CommandEncoder();
	CommandEncoder(CommandEncoder &&) noexcept;
	CommandEncoder &operator=(CommandEncoder &&) = delete;
	void step(CommandWorkBudget &budget);
	bool done() const noexcept;
	std::string take();
	EncodedCommand take_encoded();
	bool discard(CommandWorkBudget &budget);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
// Only the canonical decoder can mint this capability. The digest and
// definition sizes belong to the exact validated content and profile.
class ValidatedCommand {
  public:
	ValidatedCommand(ValidatedCommand &&other) noexcept
		: command_(std::move(other.command_)), digest_(other.digest_), limits_(other.limits_),
		  sizes_(std::move(other.sizes_)), valid_(std::exchange(other.valid_, false)) {}
	ValidatedCommand &operator=(ValidatedCommand &&) = delete;
	ValidatedCommand(const ValidatedCommand &) = delete;
	ValidatedCommand &operator=(const ValidatedCommand &) = delete;
	const Command &command() const noexcept { return command_; }
	const Digest &canonical_digest() const noexcept { return digest_; }
	const Limits &limits() const noexcept { return limits_; }
	std::size_t definition_bytes(std::size_t mutation) const { return sizes_.at(mutation); }

  private:
	friend class CommandDecoder;
	friend class CommandApplication;
	ValidatedCommand(Command command, Digest digest, Limits limits, std::vector<std::size_t> sizes)
		: command_(std::move(command)), digest_(digest), limits_(limits), sizes_(std::move(sizes)) {}
	Command command_;
	Digest digest_;
	Limits limits_;
	std::vector<std::size_t> sizes_;
	bool valid_ = true;
};
// Borrows immutable command bytes. Errors retain partial decoded ownership
// until discard() drains it; the input owner must outlive this cursor.
class CommandDecoder {
  public:
	CommandDecoder(std::string_view bytes, Limits limits = {}, std::optional<Context> context = {});
	~CommandDecoder();
	CommandDecoder(CommandDecoder &&) noexcept;
	CommandDecoder &operator=(CommandDecoder &&) = delete;
	void step(CommandWorkBudget &budget);
	bool done() const noexcept;
	ValidatedCommand take();
	bool discard(CommandWorkBudget &budget);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
struct Receipt {
	std::uint64_t sequence;
	Digest digest;
	Result result;
};
struct Producer {
	std::uint64_t generation = 0, high_water = 0;
	bool active = false;
	Identity nonce{};
	std::deque<Receipt> receipts;
};
// A capture shares immutable roots. Retain at most one export/candidate per
// runtime.
class ImageEncoder;
class ImageDecoder;
class AuthorizedScope;
class ExportCapture;
class AdministrativeApplication;
class State {
  public:
	explicit State(Context context, Limits limits = {});
	const Context &context() const noexcept { return context_; }
	const Limits &limits() const noexcept { return limits_; }
	Revision revision() const noexcept { return revision_; }
	std::uint64_t applied_index() const noexcept { return applied_; }
	std::uint64_t applied_term() const noexcept { return applied_term_; }
	std::size_t size() const noexcept { return records_.size(); }
	std::size_t logical_bytes() const noexcept { return bytes_; }
	const Record *find(std::string_view key) const { return records_.find(key); }
	Revision namespace_revision(std::string_view name) const;
	const Namespace *namespace_entry(std::string_view name) const { return namespaces_.find(name); }
	bool positions_enabled() const noexcept { return seed_ != Identity{}; }
	// The layout policy in force, and one a SetLayout entry set for the next seal.
	const LayoutPolicy &policy() const noexcept { return policy_; }
	const std::optional<LayoutPolicy> &pending_policy() const noexcept { return pending_; }
	const SealCounters &seal() const noexcept { return seal_; }
	// Closes one applied log entry of `payload_bytes`: counts it and seals when
	// the policy says so. True when this entry sealed; sealed_changes() then
	// holds each key changed since the previous seal and its last position.
	bool close_entry(std::uint64_t payload_bytes);
	template <class F> void for_each_sealed_change(F &&f) const { sealed_.for_each(std::forward<F>(f)); }
	std::size_t sealed_changes() const noexcept { return sealed_.size(); }
	std::size_t pending_changes() const noexcept { return touched_.size(); }
	// The directory's own position: one per creation, re-creation or drop.
	std::uint64_t directory_position() const noexcept { return directory_head_; }
	// Visits, in directory order, each namespace whose latest directory change is after `after`.
	template <class F> void for_each_directory_entry(std::uint64_t after, F &&f) const {
		auto cursor = directory_.cursor_from(after + 1);
		while (auto row = cursor.next())
			f(*row->first, *row->second, *namespaces_.find(*row->second));
	}
	NamespaceCursor namespace_cursor(std::string_view name) const;
	CursorAnswer answer(std::string_view name, const NamespaceCursor &cursor) const;
	const Producer *producer(std::string_view name) const;
	Result apply(std::uint64_t index, const Command &command, std::uint64_t term = 1);
	void advance(std::uint64_t index, std::uint64_t term = 1);
	std::vector<Result> apply_batch(std::uint64_t index, const std::vector<Command> &commands,
									std::uint64_t term = 1);
	Result administer(std::uint64_t index, const AdministrativeCommand &command, std::uint64_t term = 1);
	Result lookup(std::string_view producer, std::uint64_t generation, std::uint64_t sequence) const;
	void retire() noexcept {
		records_.retire_root();
		namespaces_.retire_root();
		directory_.retire_root();
		producers_.retire_root();
		touched_.retire_root();
		sealed_.retire_root();
	}
	std::size_t collect(std::size_t budget = 128) noexcept;
	std::size_t collect(CommandWorkBudget &budget) noexcept;
	bool shares_reclamation_with(const State &other) const noexcept {
		return definitions_ == other.definitions_ && producer_values_ == other.producer_values_;
	}
	bool reclamation_pending() const noexcept;
	std::uint64_t retired_values() const noexcept;
	std::uint64_t retired_value_bytes() const noexcept;
	std::size_t retained_nodes() const noexcept {
		return records_.retained_nodes() + namespaces_.retained_nodes() + directory_.retained_nodes() +
			   producers_.retained_nodes() + touched_.retained_nodes();
	}
	std::size_t allocations() const noexcept {
		return records_.allocations() + namespaces_.allocations() + directory_.allocations() +
			   producers_.allocations() + touched_.allocations();
	}
	// Owner-executor capture of the authorized public projection.
	std::shared_ptr<ExportCapture> capture(std::shared_ptr<const AuthorizedScope> scope) const;
	Snapshot snapshot(std::string_view name_space = {}) const;
	template <class F> void for_each(F &&f) const { records_.for_each(std::forward<F>(f)); }
	std::string checkpoint() const;
	static State restore(std::string_view bytes, Context context, Limits limits = {});

  private:
	friend class ExportCapture;
	friend class BatchApplication;
	friend class CommandApplication;
	friend class ImageEncoder;
	friend class ImageDecoder;
	friend class AdministrativeApplication;
	// The namespace entry a change at `name` produces, before its live size.
	Namespace changed_namespace(std::string_view name) const;
	void store_namespace(const std::string &name, std::uint64_t previous_directory, const Namespace &next);
	Context context_;
	Limits limits_;
	Revision revision_ = 0;
	std::uint64_t applied_ = 0, applied_term_ = 0;
	std::size_t bytes_ = 0;
	Identity seed_{}; // Nonzero once the enabling entry applied.
	std::uint64_t directory_head_ = 0;
	LayoutPolicy policy_{};
	std::optional<LayoutPolicy> pending_;
	SealCounters seal_{};
	// Notes one positioned change of `bytes` canonical bytes to the seal trigger.
	void count_change(std::uint64_t bytes) noexcept;
	std::shared_ptr<detail::DeferredValues<KnobDef>> definitions_;
	std::shared_ptr<detail::DeferredValues<Producer>> producer_values_;
	unsigned collection_cursor_ = 0;
	detail::PersistentMap<std::string, Record> records_;
	detail::PersistentMap<std::string, Namespace> namespaces_;
	detail::PersistentMap<std::uint64_t, std::string> directory_; // Directory position to name.
	detail::PersistentMap<std::string, std::shared_ptr<const Producer>> producers_;
	// Apply's accumulator: each key changed since the last seal and the position
	// of its last change; the closed one stays in sealed_ for the entry that sealed.
	detail::PersistentMap<std::string, Revision> touched_, sealed_;
};
// Borrows immutable batch bytes until the last command is decoded. A decoding
// failure poisons this cursor; the input owner must outlive it.
struct BatchWork {
	std::size_t bytes = 0, mutations = 0;
};
class BatchDecoder {
  public:
	BatchDecoder(std::string_view bytes, Limits limits = {}, std::size_t cap = 1024 * 1024);
	std::size_t count() const noexcept { return count_; }
	bool done() const noexcept { return !failed_ && decoded_ == count_; }
	BatchWork next_work();
	Command next();
	std::string_view next_bytes(); // Borrowed canonical-command candidate, not yet validated.

  private:
	std::string_view remaining_;
	Limits limits_;
	std::size_t count_ = 0, decoded_ = 0;
	bool failed_ = false;
};
struct BatchResult {
	State state;
	std::vector<Result> results;
};
// Applies one administrative entry in bounded steps. Enabling assigns each
// namespace present an incarnation and a directory position, in name order.
class AdministrativeApplication {
  public:
	AdministrativeApplication(const State &source, std::uint64_t index, std::uint64_t term,
							  AdministrativeCommand command);
	void step(CommandWorkBudget &budget);
	bool done() const noexcept { return phase_ == Phase::Complete && !consumed_; }
	BatchResult finish();
	bool discard(CommandWorkBudget &budget);

  private:
	enum class Phase { Inspect, Assign, Complete };
	State source_, candidate_;
	std::uint64_t index_, term_;
	AdministrativeCommand command_;
	Result result_;
	std::optional<decltype(State::namespaces_)::Cursor> cursor_;
	Phase phase_ = Phase::Inspect;
	bool consumed_ = false, retired_ = false;
};
// Owns one validated command and an unpublished candidate. Source roots and
// command storage remain alive across every equality and receipt-copy yield.
class CommandApplication {
  public:
	CommandApplication(const State &source, std::uint64_t index, std::uint64_t term,
					   ValidatedCommand command, std::shared_ptr<AppliedChangeRecorder> recorder = {});
	~CommandApplication();
	CommandApplication(CommandApplication &&) noexcept;
	CommandApplication &operator=(CommandApplication &&) = delete;
	void step(CommandWorkBudget &budget);
	bool done() const noexcept;
	BatchResult finish();
	bool discard(CommandWorkBudget &budget);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
// Preserves apply_batch semantics across yields. Only finish() exposes the
// candidate, and semantic Result rejections still advance the command group.
class BatchApplication {
  public:
	BatchApplication(const State &source, std::uint64_t index, std::uint64_t term, std::size_t count,
					 std::shared_ptr<AppliedChangeRecorder> recorder = {});
	BatchApplication(const BatchApplication &) = delete;
	BatchApplication &operator=(const BatchApplication &) = delete;
	BatchApplication(BatchApplication &&other) noexcept;
	BatchApplication &operator=(BatchApplication &&) = delete;
	void advance(const Command &command);
	void start(ValidatedCommand command);
	void step(CommandWorkBudget &budget);
	bool command_pending() const noexcept { return command_ != nullptr; }
	bool done() const noexcept { return !failed_ && !consumed_ && !command_ && results_.size() == count_; }
	BatchResult finish();
	bool discard(CommandWorkBudget &budget);

  private:
	State candidate_;
	std::uint64_t index_, term_, base_;
	std::size_t count_;
	std::vector<Result> results_;
	std::unique_ptr<CommandApplication> command_;
	std::shared_ptr<AppliedChangeRecorder> recorder_;
	bool failed_ = false, consumed_ = false;
};
// Decode into an unpublished root, retaining one bounded row and input chunk.
class ImageDecoder {
  public:
	ImageDecoder(Context context, Limits limits = {});
	~ImageDecoder();
	ImageDecoder(ImageDecoder &&) noexcept;
	ImageDecoder &operator=(ImageDecoder &&) noexcept;
	void feed(std::string_view bytes, bool final = false);
	void step(std::size_t rows = 128);
	bool needs_input() const noexcept;
	bool finished() const noexcept;
	State take();
	State discard(); // Retired roots must be collected in bounded executor steps.

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
// Incremental encoding owns a constant-time root capture and one bounded row.
class ImageEncoder {
  public:
	explicit ImageEncoder(State state);
	std::string next(); // At most 64 KiB. No IO; call from the storage executor.
	bool finished() const noexcept { return stage_ == 9 && pending_.empty(); }

  private:
	State state_;
	decltype(State::records_)::Cursor records_;
	decltype(State::namespaces_)::Cursor namespaces_;
	decltype(State::producers_)::Cursor producers_;
	decltype(State::touched_)::Cursor touched_;
	unsigned stage_ = 0;
	std::string pending_;
	std::size_t offset_ = 0, written_ = 0;
	void row();
};
// Narrow internal versioned command codec, independent of public wire
// selection. Encoding sorts mutations and override keys; semantic digest
// ignores JSON layout.
std::string encode(const Command &command, Limits limits = {});
// An administrative entry's payload, distinct from a transaction group's.
std::string encode(const AdministrativeCommand &command, Limits limits = {});
AdministrativeCommand decode_administrative(std::string_view bytes, Limits limits = {});
bool is_administrative(std::string_view payload) noexcept;
// The incarnation a namespace takes at directory position `directory`.
Identity derive_incarnation(const Context &context, const Identity &seed, std::uint64_t directory,
							std::string_view name);
Command decode(std::string_view bytes, Limits limits = {});
// A definition's canonical encoding: the bytes a live size counts beside the key and 16.
std::string encode_definition(const KnobDef &definition, const Limits &limits = {});
KnobDef decode_definition(std::string_view bytes, const Limits &limits = {});
Digest digest(const Command &command, Limits limits = {});
void validate(const Command &command, Limits limits = {});
// Canonical application profile shared by batches, snapshots and peer admission.
std::string semantic_profile(Limits limits = {});
std::size_t batch_framing_bytes(Limits limits = {});
std::string batch_header(std::size_t count, Limits limits = {});
bool dispose_command(Command &command, CommandWorkBudget &budget) noexcept;
std::string encode_batch(const std::vector<Command> &commands, Limits limits = {},
						 std::size_t maximum_bytes = 1024 * 1024);
std::vector<Command> decode_batch(std::string_view bytes, Limits limits = {},
								  std::size_t maximum_bytes = 1024 * 1024);
} // namespace detent::authority
