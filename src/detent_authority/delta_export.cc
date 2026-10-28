#include "delta_export.hh"
#include "detent_distribution/wire_codec.hh"
#include <algorithm>
namespace detent::authority {
namespace d = distribution;
namespace m = msgpack::bounded;
namespace {
struct HistoryDeltaSource final : d::DeltaSource {
	struct Entry {
		std::shared_ptr<const CachedChange> change;
		std::uint32_t ns;
	};
	using Entries = std::vector<Entry, d::StorageAllocator<Entry>>;
	std::shared_ptr<ChangeHistory> history;
	std::optional<ChangeHistory::Handoff> handoff;
	std::shared_ptr<const AuthorizedScope> scope;
	d::Cursor base_, target_;
	d::Limits limits;
	Entries entries;
	m::InlineBytes<512> scratch;
	std::string_view pending;
	std::optional<detent::distribution::WireDecoder> decoder;
	std::span<const char> input;
	std::uint64_t revision, mutations = 0, overrides = 0, strings = 0;
	std::uint64_t children = 0;
	std::size_t entry = 0, skipped = 0, token_start = 0, credit = 0, disposal = 0;
	bool prepared_ = false, discarded_ = false;
	HistoryDeltaSource(std::shared_ptr<ChangeHistory> source, std::shared_ptr<const AuthorizedScope> names,
					   const Context &ctx, d::Cursor from, d::Cursor to, d::Limits bounds,
					   std::optional<ChangeHistory::Handoff> marker)
		: history(std::move(source)), scope(std::move(names)), base_(from), target_(to), limits(bounds),
		  entries(d::StorageAllocator<Entry>(scope->storage_account())), revision(base_.revision) {
		if (!base_.same_identity(target_) || base_.scope != scope->identity() ||
			base_.cluster != ctx.cluster || base_.group != ctx.group || base_.restore_epoch != ctx.epoch ||
			target_.revision <= base_.revision || scope->size() > limits.namespaces ||
			history->storage_account() != scope->storage_account() ||
			(limits.storage_account && limits.storage_account != scope->storage_account()))
			throw std::invalid_argument("delta export identity or admission mismatch");
		handoff = marker ? std::move(marker) : history->handoff(ctx, base_.revision);
		if (handoff && handoff->revision() != base_.revision)
			throw std::invalid_argument("delta export handoff base mismatch");
		if (!handoff || history->continuity(*handoff, target_.revision) != ChangeHistory::Continuity::Ready)
			throw SnapshotRequired(SnapshotRequired::Reason::HistoryLost);
		entries.reserve(std::min<std::uint64_t>(history->size(), target_.revision - base_.revision));
	}
	const d::Cursor &base() const noexcept override { return base_; }
	const d::Cursor &target() const noexcept override { return target_; }
	std::shared_ptr<d::StorageLedger> storage_account() const noexcept override {
		return scope->storage_account();
	}
	bool prepared() const noexcept override { return prepared_; }
	d::DeltaCounts counts() const noexcept override {
		return {scope->size(), entries.size(), mutations, overrides, strings};
	}
	void gather() {
		if (history->continuity(*handoff, target_.revision) != ChangeHistory::Continuity::Ready)
			throw SnapshotRequired(SnapshotRequired::Reason::HistoryLost);
		if (revision == target_.revision) {
			history.reset();
			prepared_ = true;
			return;
		}
		auto change = history->find(++revision);
		if (!change)
			throw SnapshotRequired(SnapshotRequired::Reason::HistoryLost);
		std::uint64_t low = 0, high = scope->size();
		while (low < high) {
			const auto mid = low + (high - low) / 2;
			if (scope->name(mid) < change->name_space())
				low = mid + 1;
			else
				high = mid;
		}
		if (low == scope->size() || scope->name(low) != change->name_space())
			return;
		if (change->mutations > limits.records - mutations || entries.size() == limits.records ||
			change->strings > limits.string_bytes - strings ||
			change->maximum_definition_bytes > limits.definition_bytes ||
			change->maximum_overrides > limits.overrides_per_definition)
			throw DeltaRepresentationLimit("delta export aggregate limit");
		mutations += change->mutations;
		overrides += change->overrides;
		strings += change->strings;
		entries.push_back({std::move(change), static_cast<std::uint32_t>(low)});
	}
	void transaction_header() {
		const auto &value = entries[entry];
		m::AppendPacker(scratch).pack_array(3);
		m::AppendPacker(scratch).pack_uint64(value.change->revision);
		m::AppendPacker(scratch).pack_array(1);
		m::AppendPacker(scratch).pack_array(2);
		m::AppendPacker(scratch).pack_str(value.change->name_space());
		m::AppendPacker(scratch).pack_uint64(value.change->revision);
		m::AppendPacker(scratch).pack_array(value.change->mutations);
		decoder.emplace(m::Limits{value.change->body->size(), 200, 32, UINT32_MAX});
		input = {value.change->body->data(), value.change->body->size()};
		skipped = token_start = 0;
		children = 0;
	}
	// Cached input is internal canonical bytes. Decode one lexical token to
	// identify each mutation boundary, then reuse its immutable byte slice.
	void token(d::WorkBudget &budget) {
		auto value = decoder->next(input, budget.bytes);
		if (!value)
			return;
		const auto end = static_cast<std::size_t>(decoder->consumed());
		auto raw = entries[entry].change->body->view().substr(token_start, end - token_start);
		token_start = end;
		if (skipped < 5) {
			++skipped;
			return;
		}
		if (!children) {
			if (value->kind != m::Kind::Array || value->number != 3)
				throw std::logic_error("invalid cached mutation boundary");
			children = 3;
			m::AppendPacker(scratch).pack_array(4);
			m::AppendPacker(scratch).pack_uint64(entries[entry].ns);
			pending = scratch;
		} else {
			--children;
			if (value->kind == m::Kind::Array)
				children += value->number;
			pending = raw;
		}
		if (input.empty()) {
			decoder->finish();
			if (children)
				throw std::logic_error("incomplete cached transaction");
			decoder.reset();
			++entry;
		}
	}
	void prepare(d::WorkBudget &budget) override {
		if (discarded_)
			throw std::logic_error("history delta source discarded");
		while (!prepared_ && budget.tokens && budget.bytes) {
			if (!credit)
				credit = 512;
			const auto n = std::min(credit, budget.bytes);
			credit -= n;
			budget.bytes -= n;
			if (credit)
				return;
			--budget.tokens;
			gather();
		}
	}
	void reset_fragments() noexcept override {
		pending = {};
		scratch.clear();
		decoder.reset();
		input = {};
		entry = skipped = token_start = credit = 0;
		children = 0;
	}
	d::DeltaFragmentStep next_fragment(d::WorkBudget &budget) override {
		if (!prepared_ || discarded_)
			throw std::logic_error("history delta source not ready");
		pending = {};
		scratch.clear();
		if (!budget.tokens || !budget.bytes)
			return {};
		if (!decoder) {
			if (!credit)
				credit = 512;
			const auto n = std::min(credit, budget.bytes);
			credit -= n;
			budget.bytes -= n;
			if (credit)
				return {};
		}
		--budget.tokens;
		if (entry == entries.size())
			return {{}, true};
		if (!decoder) {
			transaction_header();
			pending = scratch;
		} else
			token(budget);
		return {pending, false};
	}
	bool discard(d::WorkBudget &budget) noexcept override {
		discarded_ = true;
		reset_fragments();
		while (!entries.empty()) {
			if (!budget.tokens)
				return false;
			if (!disposal)
				disposal = entries.back().change->body->storage_capacity();
			const auto n = std::min(disposal, budget.bytes);
			disposal -= n;
			budget.bytes -= n;
			if (disposal)
				return false;
			--budget.tokens;
			entries.pop_back();
		}
		if (entries.capacity()) {
			if (!budget.tokens)
				return false;
			if (!disposal)
				disposal = entries.capacity() * sizeof(Entry);
			const auto n = std::min(disposal, budget.bytes);
			disposal -= n;
			budget.bytes -= n;
			if (disposal)
				return false;
			--budget.tokens;
			Entries empty(entries.get_allocator());
			entries.swap(empty);
		}
		history.reset();
		return true;
	}
};
std::shared_ptr<d::DeltaSource> source(std::shared_ptr<ChangeHistory> history,
									   std::shared_ptr<const AuthorizedScope> scope, const Context &context,
									   d::Cursor base, d::Cursor target, d::Limits limits,
									   std::optional<ChangeHistory::Handoff> handoff) {
	if (!history || !scope)
		throw std::invalid_argument("missing delta export owner");
	return std::allocate_shared<HistoryDeltaSource>(
		d::StorageAllocator<HistoryDeltaSource>(scope->storage_account()), history, scope, context, base,
		target, limits, std::move(handoff));
}
} // namespace
DeltaExportEncoder::DeltaExportEncoder(std::shared_ptr<ChangeHistory> history,
									   std::shared_ptr<const AuthorizedScope> scope, const Context &context,
									   d::Cursor base, d::Cursor target, d::Limits limits,
									   std::optional<ChangeHistory::Handoff> handoff)
	: encoder_(
		  source(std::move(history), std::move(scope), context, base, target, limits, std::move(handoff)),
		  limits) {}
DeltaExportEncoder::~DeltaExportEncoder() = default;
void DeltaExportEncoder::step(d::WorkBudget &budget) { encoder_.step(budget); }
bool DeltaExportEncoder::counted() const noexcept { return encoder_.counted(); }
std::uint64_t DeltaExportEncoder::encoded_length() const { return encoder_.encoded_length(); }
d::UpdateKind DeltaExportEncoder::kind() const { return encoder_.kind(); }
bool DeltaExportEncoder::done() const noexcept { return encoder_.done(); }
bool DeltaExportEncoder::chunk_ready() const noexcept { return encoder_.chunk_ready(); }
std::shared_ptr<const d::OwnedBuffer> DeltaExportEncoder::take_chunk() { return encoder_.take_chunk(); }
bool DeltaExportEncoder::discard(d::WorkBudget &budget) noexcept { return encoder_.discard(budget); }
} // namespace detent::authority
