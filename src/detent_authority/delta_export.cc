#include "delta_export.hh"
#include "detent_distribution/messagepack.hh"
#include <algorithm>
namespace detent::authority {
namespace d = distribution;
namespace m = d::messagepack;
struct DeltaExportEncoder::Impl {
	struct Entry {
		std::shared_ptr<const CachedChange> change;
		std::uint32_t ns;
	};
	using Entries = std::vector<Entry, d::StorageAllocator<Entry>>;
	std::shared_ptr<ChangeHistory> history;
	std::shared_ptr<const AuthorizedScope> scope;
	Context context;
	d::Cursor base, target;
	d::Limits limits;
	Entries entries;
	std::shared_ptr<d::OwnedBuffer> output;
	m::InlineBytes<512> scratch;
	std::string_view pending;
	std::optional<m::WireDecoder> decoder;
	std::span<const char> input;
	std::uint64_t revision, mutations = 0, overrides = 0, strings = 0, length = 0, emitted = 0;
	std::uint64_t children = 0;
	std::size_t entry = 0, skipped = 0, token_start = 0, offset = 0, credit = 0, disposal = 0;
	unsigned phase = 0;
	bool emitting = false, ready = false, finished = false, failed = false;
	Impl(std::shared_ptr<ChangeHistory> source, std::shared_ptr<const AuthorizedScope> names,
		 const Context &ctx, d::Cursor from, d::Cursor to, d::Limits bounds)
		: history(std::move(source)), scope(std::move(names)), context(ctx), base(from), target(to),
		  limits(bounds), entries(d::StorageAllocator<Entry>(scope->storage_account())),
		  revision(base.revision) {
		if (!base.same_identity(target) || base.scope != scope->identity() || base.cluster != ctx.cluster ||
			base.group != ctx.group || base.restore_epoch != ctx.epoch || target.revision <= base.revision ||
			scope->size() > limits.namespaces || history->storage_account() != scope->storage_account() ||
			(limits.storage_account && limits.storage_account != scope->storage_account()) ||
			!history->covers(ctx, base.revision, target.revision))
			throw std::invalid_argument("delta export identity or coverage mismatch");
		entries.reserve(std::min<std::uint64_t>(history->size(), target.revision - base.revision));
	}
	void count(std::size_t n) {
		if (n > limits.bytes - length)
			throw std::length_error("delta export wire limit");
		length += n;
	}
	void cursor(const d::Cursor &value) {
		m::array(scratch, 5);
		for (const auto *id : {&value.cluster, &value.group, &value.restore_epoch})
			m::string(scratch, {id->data(), id->size()}, true);
		m::string(scratch, {value.scope.data(), value.scope.size()}, true);
		m::unsigned_integer(scratch, value.revision);
	}
	void header() {
		m::array(scratch, entries.empty() ? 5 : 7);
		m::unsigned_integer(scratch, 1);
		m::unsigned_integer(scratch, entries.empty() ? 3 : 2);
		m::array(scratch, 2);
		m::unsigned_integer(scratch, 1);
		m::unsigned_integer(scratch, 1);
		cursor(base);
		cursor(target);
		if (!entries.empty()) {
			m::array(scratch, 4);
			m::unsigned_integer(scratch, entries.size());
			m::unsigned_integer(scratch, mutations);
			m::unsigned_integer(scratch, overrides);
			m::unsigned_integer(scratch, strings);
			m::array(scratch, entries.size());
		}
	}
	void gather() {
		if (revision == target.revision) {
			if (!history->covers(context, base.revision, target.revision))
				throw std::runtime_error("delta export history changed during capture");
			history.reset();
			phase = 1;
			return;
		}
		auto change = history->find(++revision);
		if (!change)
			throw std::runtime_error("delta export coverage lost");
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
			throw std::length_error("delta export aggregate limit");
		mutations += change->mutations;
		overrides += change->overrides;
		strings += change->strings;
		entries.push_back({std::move(change), static_cast<std::uint32_t>(low)});
	}
	void transaction_header() {
		const auto &value = entries[entry];
		m::array(scratch, 3);
		m::unsigned_integer(scratch, value.change->revision);
		m::array(scratch, 1);
		m::array(scratch, 2);
		m::string(scratch, value.change->name_space());
		m::unsigned_integer(scratch, value.change->revision);
		m::array(scratch, value.change->mutations);
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
			m::array(scratch, 4);
			m::unsigned_integer(scratch, entries[entry].ns);
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
	void action(d::WorkBudget &budget) {
		if (phase == 0) {
			gather();
			return;
		}
		if (phase == 1) {
			header();
			pending = scratch;
			phase = 2;
			return;
		}
		if (entry == entries.size()) {
			if (!emitting) {
				emitting = true;
				phase = 1;
				entry = 0;
				output = d::make_owned_buffer(scope->storage_account(), 65536);
			} else {
				if (emitted != length)
					throw std::logic_error("delta count/emission mismatch");
				finished = true;
			}
			return;
		}
		if (!decoder) {
			transaction_header();
			pending = scratch;
		} else
			token(budget);
	}
};
DeltaExportEncoder::DeltaExportEncoder(std::shared_ptr<ChangeHistory> history,
									   std::shared_ptr<const AuthorizedScope> scope, const Context &context,
									   d::Cursor base, d::Cursor target, d::Limits limits)
	: impl_([&] {
		  if (!history || !scope)
			  throw std::invalid_argument("missing delta export owner");
		  return d::make_storage_owner<Impl>(scope->storage_account(), std::malloc, std::free, history, scope,
											 context, base, target, limits);
	  }()) {}
DeltaExportEncoder::~DeltaExportEncoder() = default;
void DeltaExportEncoder::step(d::WorkBudget &budget) {
	auto &p = *impl_;
	if (p.failed)
		throw std::logic_error("inactive delta export");
	try {
		while (!p.ready && !p.finished) {
			if (!p.pending.empty()) {
				if (!p.emitting) {
					p.count(p.pending.size());
					p.pending = {};
					p.scratch.clear();
				} else {
					if (!p.output) {
						if (!budget.tokens || !budget.bytes)
							return;
						--budget.tokens;
						p.output = d::make_owned_buffer(p.scope->storage_account(), 65536);
					}
					const auto n =
						std::min({p.pending.size() - p.offset, budget.bytes, 65536 - p.output->size()});
					if (!n)
						return;
					p.output->append(p.pending.substr(p.offset, n));
					p.offset += n;
					p.emitted += n;
					budget.bytes -= n;
					if (p.offset == p.pending.size()) {
						p.pending = {};
						p.scratch.clear();
						p.offset = 0;
					}
					if (p.output->size() == 65536) {
						p.output->seal();
						p.ready = true;
						return;
					}
				}
				continue;
			}
			if (!budget.tokens || !budget.bytes)
				return;
			if (!p.decoder) {
				if (!p.credit)
					p.credit = 512;
				const auto n = std::min(p.credit, budget.bytes);
				p.credit -= n;
				budget.bytes -= n;
				if (p.credit)
					return;
			}
			--budget.tokens;
			p.action(budget);
		}
		if (p.finished && p.output && !p.output->empty() && !p.ready) {
			p.output->seal_prefix();
			p.ready = true;
		}
	} catch (...) {
		p.failed = true;
		throw;
	}
}
bool DeltaExportEncoder::counted() const noexcept { return impl_ && impl_->emitting && !impl_->failed; }
std::uint64_t DeltaExportEncoder::encoded_length() const {
	if (!counted())
		throw std::logic_error("delta export count incomplete");
	return impl_->length;
}
d::UpdateKind DeltaExportEncoder::kind() const {
	if (!counted())
		throw std::logic_error("delta export count incomplete");
	return impl_->entries.empty() ? d::UpdateKind::Progress : d::UpdateKind::Delta;
}
bool DeltaExportEncoder::done() const noexcept {
	return impl_ && impl_->finished && !impl_->ready && !impl_->failed;
}
bool DeltaExportEncoder::chunk_ready() const noexcept { return impl_ && impl_->ready && !impl_->failed; }
std::shared_ptr<const d::OwnedBuffer> DeltaExportEncoder::take_chunk() {
	if (!chunk_ready())
		throw std::logic_error("delta export chunk unavailable");
	impl_->ready = false;
	return std::exchange(impl_->output, {});
}
bool DeltaExportEncoder::discard(d::WorkBudget &budget) noexcept {
	auto &p = *impl_;
	p.failed = true;
	p.pending = {};
	p.decoder.reset();
	p.input = {};
	while (!p.entries.empty()) {
		if (!budget.tokens)
			return false;
		if (!p.disposal)
			p.disposal = p.entries.back().change->body->storage_capacity();
		auto n = std::min(p.disposal, budget.bytes);
		p.disposal -= n;
		budget.bytes -= n;
		if (p.disposal)
			return false;
		--budget.tokens;
		p.entries.pop_back();
	}
	if (p.output) {
		if (!budget.tokens)
			return false;
		if (!p.disposal)
			p.disposal = p.output->storage_capacity();
		auto n = std::min(p.disposal, budget.bytes);
		p.disposal -= n;
		budget.bytes -= n;
		if (p.disposal)
			return false;
		--budget.tokens;
		p.output.reset();
	}
	if (p.entries.capacity()) {
		if (!budget.tokens)
			return false;
		if (!p.disposal)
			p.disposal = p.entries.capacity() * sizeof(Impl::Entry);
		auto n = std::min(p.disposal, budget.bytes);
		p.disposal -= n;
		budget.bytes -= n;
		if (p.disposal)
			return false;
		--budget.tokens;
		Impl::Entries empty(p.entries.get_allocator());
		p.entries.swap(empty);
	}
	p.history.reset();
	return true;
}
} // namespace detent::authority
