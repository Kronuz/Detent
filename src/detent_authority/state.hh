#pragma once

#include "detent/model.hh"
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
struct Limits {
	std::size_t keys = 500000, namespaces = 4096, producers = 1024;
	std::size_t mutations = 128, request_bytes = 256 * 1024, state_bytes = 128 * 1024 * 1024,
				image_bytes = 256 * 1024 * 1024;
	std::size_t definition_bytes = 16 * 1024, overrides = 256, results = 256;
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
	const Producer *producer(std::string_view name) const;
	Result apply(std::uint64_t index, const Command &command, std::uint64_t term = 1);
	void advance(std::uint64_t index, std::uint64_t term = 1);
	std::vector<Result> apply_batch(std::uint64_t index, const std::vector<Command> &commands,
									std::uint64_t term = 1);
	Result lookup(std::string_view producer, std::uint64_t generation, std::uint64_t sequence) const;
	void retire() noexcept {
		records_.retire_root();
		namespaces_.retire_root();
		producers_.retire_root();
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
		return records_.retained_nodes() + namespaces_.retained_nodes() + producers_.retained_nodes();
	}
	std::size_t allocations() const noexcept {
		return records_.allocations() + namespaces_.allocations() + producers_.allocations();
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
	Context context_;
	Limits limits_;
	Revision revision_ = 0;
	std::uint64_t applied_ = 0, applied_term_ = 0;
	std::size_t bytes_ = 0;
	std::shared_ptr<detail::DeferredValues<KnobDef>> definitions_;
	std::shared_ptr<detail::DeferredValues<Producer>> producer_values_;
	unsigned collection_cursor_ = 0;
	detail::PersistentMap<std::string, Record> records_;
	detail::PersistentMap<std::string, Revision> namespaces_;
	detail::PersistentMap<std::string, std::shared_ptr<const Producer>> producers_;
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
// Owns one validated command and an unpublished candidate. Source roots and
// command storage remain alive across every equality and receipt-copy yield.
class CommandApplication {
  public:
	CommandApplication(const State &source, std::uint64_t index, std::uint64_t term,
					   ValidatedCommand command);
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
	BatchApplication(const State &source, std::uint64_t index, std::uint64_t term, std::size_t count);
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
	bool finished() const noexcept { return stage_ == 7 && pending_.empty(); }

  private:
	State state_;
	decltype(State::records_)::Cursor records_;
	decltype(State::namespaces_)::Cursor namespaces_;
	decltype(State::producers_)::Cursor producers_;
	unsigned stage_ = 0;
	std::string pending_;
	std::size_t offset_ = 0, written_ = 0;
	void row();
};
// Narrow internal versioned command codec, independent of public wire
// selection. Encoding sorts mutations and override keys; semantic digest
// ignores JSON layout.
std::string encode(const Command &command, Limits limits = {});
Command decode(std::string_view bytes, Limits limits = {});
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
