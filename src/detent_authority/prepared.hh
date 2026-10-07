#pragma once
#include "state.hh"
namespace detent::authority {
class ProposalBuilder;
class Node;
class EncodedCommand {
  public:
	EncodedCommand(EncodedCommand &&other) noexcept
		: bytes_(std::move(other.bytes_)), context_(other.context_), limits_(other.limits_),
		  valid_(std::exchange(other.valid_, false)) {}
	EncodedCommand(const EncodedCommand &) = delete;
	EncodedCommand &operator=(EncodedCommand &&) = delete;
	std::size_t size() const noexcept { return bytes_.size(); }

  private:
	friend class CommandEncoder;
	friend class ProposalBuilder;
	EncodedCommand(std::string bytes, Context context, Limits limits)
		: bytes_(std::move(bytes)), context_(context), limits_(limits) {}
	std::string bytes_;
	Context context_;
	Limits limits_;
	bool valid_ = true;
};
class PreparedProposal {
  public:
	PreparedProposal(PreparedProposal &&other) noexcept
		: bytes_(std::move(other.bytes_)), digest_(other.digest_), context_(other.context_),
		  limits_(other.limits_), valid_(std::exchange(other.valid_, false)) {}
	PreparedProposal(const PreparedProposal &) = delete;
	PreparedProposal &operator=(PreparedProposal &&) = delete;
	const std::string &bytes() const noexcept { return bytes_; }
	const Digest &digest() const noexcept { return digest_; }
	bool valid() const noexcept { return valid_; }

  private:
	friend class ProposalBuilder;
	friend class Node;
	PreparedProposal(std::string bytes, Digest digest, Context context, Limits limits)
		: bytes_(std::move(bytes)), digest_(digest), context_(context), limits_(limits) {}
	std::string bytes_;
	Digest digest_;
	Context context_;
	Limits limits_;
	bool valid_ = true;
};
// One reserved batch buffer and one in-progress encoded command. A rejected
// append retains its input capability unchanged for the next batch.
class ProposalBuilder {
  public:
	ProposalBuilder(Context context, Limits limits = {}, std::size_t cap = 1024 * 1024);
	~ProposalBuilder();
	ProposalBuilder(ProposalBuilder &&) noexcept;
	ProposalBuilder &operator=(ProposalBuilder &&) = delete;
	bool append(EncodedCommand &command);
	void step(CommandWorkBudget &budget);
	bool command_pending() const noexcept;
	std::size_t count() const noexcept;
	void seal();
	bool done() const noexcept;
	PreparedProposal take();
	void discard() noexcept;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::authority
