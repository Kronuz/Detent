#include "prepared.hh"
#include <algorithm>
#include <sha256.h>
#include <stdexcept>
namespace detent::authority {
namespace {
void check(bool condition, const char *message) {
	if (!condition)
		throw std::invalid_argument(message);
}
void number(std::string &bytes, std::uint64_t value) {
	for (unsigned i = 0; i < 8; ++i)
		bytes.push_back(static_cast<char>(value >> (i * 8)));
}
} // namespace
struct ProposalBuilder::Impl {
	Context context;
	Limits limits;
	std::size_t cap, count = 0, copied = 0, hashed = 0, count_offset;
	std::string bytes;
	std::optional<EncodedCommand> command;
	SHA256 sha;
	Digest digest{};
	bool prefix = false, sealed = false, complete = false, consumed = false;
	Impl(Context c, Limits l, std::size_t bound) : context(c), limits(l), cap(bound) {
		check(cap >= batch_framing_bytes(l) && cap <= 64 * 1024 * 1024, "proposal byte bound");
		check(c.cluster != Identity{} && c.group != Identity{} && c.epoch != Identity{},
			  "missing authority context");
		bytes = batch_header(0, l);
		bytes.reserve(cap);
		count_offset = bytes.size() - 8;
	}
};
ProposalBuilder::ProposalBuilder(Context context, Limits limits, std::size_t cap)
	: impl_(std::make_unique<Impl>(context, limits, cap)) {}
ProposalBuilder::~ProposalBuilder() = default;
ProposalBuilder::ProposalBuilder(ProposalBuilder &&) noexcept = default;
bool ProposalBuilder::append(EncodedCommand &command) {
	check(impl_ && !impl_->sealed && !impl_->consumed && !impl_->command, "proposal builder unavailable");
	auto &d = *impl_;
	check(command.valid_ && command.context_ == d.context && command.limits_ == d.limits,
		  "foreign encoded command capability");
	if (d.count == 64 || d.cap - d.bytes.size() < 8 || command.bytes_.size() > d.cap - d.bytes.size() - 8)
		return false;
	d.command.emplace(std::move(command));
	d.copied = 0;
	d.prefix = false;
	return true;
}
void ProposalBuilder::step(CommandWorkBudget &budget) {
	check(impl_ && !impl_->consumed, "proposal builder unavailable");
	auto &d = *impl_;
	if (d.command) {
		if (!d.prefix) {
			if (!budget.records || budget.bytes < 128)
				return;
			--budget.records;
			budget.bytes -= 128;
			number(d.bytes, d.command->bytes_.size());
			d.prefix = true;
		}
		auto amount = std::min(budget.bytes, d.command->bytes_.size() - d.copied);
		d.bytes.append(d.command->bytes_.data() + d.copied, amount);
		d.copied += amount;
		budget.bytes -= amount;
		if (d.copied == d.command->bytes_.size()) {
			++d.count;
			d.command.reset();
		}
	}
	if (d.sealed && !d.complete) {
		auto amount = std::min(budget.bytes, d.bytes.size() - d.hashed);
		d.sha.add(d.bytes.data() + d.hashed, amount);
		d.hashed += amount;
		budget.bytes -= amount;
		if (d.hashed == d.bytes.size() && budget.records && budget.bytes >= 128) {
			--budget.records;
			budget.bytes -= 128;
			d.sha.getHash(d.digest.data());
			d.complete = true;
		}
	}
}
bool ProposalBuilder::command_pending() const noexcept { return impl_ && impl_->command.has_value(); }
std::size_t ProposalBuilder::count() const noexcept { return impl_ ? impl_->count : 0; }
void ProposalBuilder::seal() {
	check(impl_ && !impl_->consumed && !impl_->sealed && !impl_->command && impl_->count,
		  "proposal cannot seal");
	auto &d = *impl_;
	for (unsigned i = 0; i < 8; ++i)
		d.bytes[d.count_offset + i] = static_cast<char>(static_cast<std::uint64_t>(d.count) >> (i * 8));
	d.sealed = true;
}
bool ProposalBuilder::done() const noexcept { return impl_ && !impl_->consumed && impl_->complete; }
PreparedProposal ProposalBuilder::take() {
	check(done(), "proposal incomplete");
	impl_->consumed = true;
	return {std::move(impl_->bytes), impl_->digest, impl_->context, impl_->limits};
}
void ProposalBuilder::discard() noexcept { impl_.reset(); }
} // namespace detent::authority
