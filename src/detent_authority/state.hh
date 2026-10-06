#pragma once

#include "detent/model.hh"
#include "persistent_map.hh"
#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace detent::authority {
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
	std::size_t collect(std::size_t budget = 128) noexcept {
		auto n = records_.collect(budget);
		n += namespaces_.collect(budget);
		n += producers_.collect(budget);
		return n;
	}
	bool reclamation_pending() const noexcept {
		return records_.reclamation_pending() || namespaces_.reclamation_pending() ||
			   producers_.reclamation_pending();
	}
	std::size_t retained_nodes() const noexcept {
		return records_.retained_nodes() + namespaces_.retained_nodes() + producers_.retained_nodes();
	}
	std::size_t allocations() const noexcept {
		return records_.allocations() + namespaces_.allocations() + producers_.allocations();
	}
	Snapshot snapshot(std::string_view name_space = {}) const;
	template <class F> void for_each(F &&f) const { records_.for_each(std::forward<F>(f)); }
	std::string checkpoint() const;
	static State restore(std::string_view bytes, Context context, Limits limits = {});

  private:
	friend class BatchApplication;
	friend class ImageEncoder;
	friend class ImageDecoder;
	Context context_;
	Limits limits_;
	Revision revision_ = 0;
	std::uint64_t applied_ = 0, applied_term_ = 0;
	std::size_t bytes_ = 0;
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
	bool done() const noexcept { return !failed_ && !consumed_ && results_.size() == count_; }
	BatchResult finish();

  private:
	State candidate_;
	std::uint64_t index_, term_, base_;
	std::size_t count_;
	std::vector<Result> results_;
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
std::string encode_batch(const std::vector<Command> &commands, Limits limits = {},
						 std::size_t maximum_bytes = 1024 * 1024);
std::vector<Command> decode_batch(std::string_view bytes, Limits limits = {},
								  std::size_t maximum_bytes = 1024 * 1024);
} // namespace detent::authority
