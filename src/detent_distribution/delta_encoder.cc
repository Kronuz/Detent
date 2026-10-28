#include "detent_distribution/delta_encoder.hh"
#include "detent_distribution/wire_codec.hh"
#include <algorithm>

namespace detent::distribution {
namespace m = msgpack::bounded;
struct DeltaEncoder::Impl {
	static constexpr std::size_t chunk_bytes = 65536;
	std::shared_ptr<DeltaSource> source;
	Limits limits;
	DeltaCounts counts;
	std::shared_ptr<OwnedBuffer> output;
	m::InlineBytes<512> scratch;
	std::string_view pending;
	std::uint64_t length = 0, emitted = 0;
	std::size_t offset = 0, credit = 0, disposal = 0;
	unsigned phase = 0;
	bool emitting = false, ready = false, finished = false, failed = false;
	Impl(std::shared_ptr<DeltaSource> input, Limits bounds)
		: source(std::move(input)), limits(std::move(bounds)) {}
	void check_counts() {
		counts = source->counts();
		if (counts.namespaces > limits.namespaces || counts.transactions > limits.records ||
			counts.mutations > limits.records || counts.strings > limits.string_bytes ||
			counts.maximum_definition_bytes > limits.definition_bytes ||
			counts.maximum_overrides > limits.overrides_per_definition)
			throw DeltaRepresentationLimit("delta export aggregate limit");
	}
	void put_cursor(const Cursor &value) {
		m::AppendPacker(scratch).pack_array(5);
		for (const auto *id : {&value.cluster, &value.group, &value.restore_epoch})
			m::AppendPacker(scratch).pack_bin({id->data(), id->size()});
		m::AppendPacker(scratch).pack_bin({value.scope.data(), value.scope.size()});
		m::AppendPacker(scratch).pack_uint64(value.revision);
	}
	void header() {
		m::AppendPacker(scratch).pack_array(counts.transactions ? 7 : 5);
		m::AppendPacker(scratch).pack_uint64(1);
		m::AppendPacker(scratch).pack_uint64(counts.transactions ? 2 : 3);
		m::AppendPacker(scratch).pack_array(2);
		m::AppendPacker(scratch).pack_uint64(1);
		m::AppendPacker(scratch).pack_uint64(1);
		put_cursor(source->base());
		put_cursor(source->target());
		if (counts.transactions) {
			m::AppendPacker(scratch).pack_array(4);
			for (auto value : {counts.transactions, counts.mutations, counts.overrides, counts.strings})
				m::AppendPacker(scratch).pack_uint64(value);
			m::AppendPacker(scratch).pack_array(counts.transactions);
		}
		pending = scratch;
	}
};
DeltaEncoder::DeltaEncoder(std::shared_ptr<DeltaSource> source, Limits limits)
	: impl_([&] {
		  if (!source || !source->storage_account() || !source->base().same_identity(source->target()) ||
			  source->target().revision <= source->base().revision ||
			  (limits.storage_account && limits.storage_account != source->storage_account()))
			  throw std::invalid_argument("delta source identity or account mismatch");
		  return make_storage_owner<Impl>(source->storage_account(), std::malloc, std::free, source, limits);
	  }()) {}
DeltaEncoder::~DeltaEncoder() = default;
void DeltaEncoder::step(WorkBudget &budget) {
	auto &p = *impl_;
	if (p.failed)
		throw std::logic_error("inactive delta encoder");
	try {
		while (!p.ready && !p.finished) {
			if (!p.pending.empty()) {
				if (!p.emitting) {
					if (p.pending.size() > p.limits.bytes - p.length)
						throw DeltaRepresentationLimit("delta export wire limit");
					p.length += p.pending.size();
					p.pending = {};
					p.scratch.clear();
				} else {
					if (!budget.bytes)
						return;
					if (!p.output) {
						if (!budget.tokens)
							return;
						--budget.tokens;
						p.output = make_owned_buffer(p.source->storage_account(), Impl::chunk_bytes);
					}
					const auto n = std::min(
						{p.pending.size() - p.offset, budget.bytes, Impl::chunk_bytes - p.output->size()});
					if (!n)
						return;
					if (n > p.length - p.emitted)
						throw std::logic_error("delta source count/emission mismatch");
					p.output->append(p.pending.substr(p.offset, n));
					p.offset += n;
					p.emitted += n;
					budget.bytes -= n;
					if (p.offset == p.pending.size()) {
						p.pending = {};
						p.scratch.clear();
						p.offset = 0;
					}
					if (p.output->size() == Impl::chunk_bytes) {
						p.output->seal();
						p.ready = true;
						return;
					}
				}
				continue;
			}
			if (!budget.tokens || !budget.bytes)
				return;
			if (p.phase == 0) {
				p.source->prepare(budget);
				if (!p.source->prepared())
					return;
				p.check_counts();
				p.source->reset_fragments();
				p.phase = 1;
				continue;
			}
			if (p.phase == 1) {
				if (!p.credit)
					p.credit = 512;
				const auto n = std::min(p.credit, budget.bytes);
				p.credit -= n;
				budget.bytes -= n;
				if (p.credit)
					return;
				--budget.tokens;
				p.header();
				p.phase = 2;
				continue;
			}
			auto next = p.source->next_fragment(budget);
			if (next.done) {
				if (!next.bytes.empty())
					throw std::logic_error("delta source terminal fragment mismatch");
				if (!p.emitting) {
					p.emitting = true;
					p.source->reset_fragments();
					p.phase = 1;
				} else {
					if (p.emitted != p.length)
						throw std::logic_error("delta source count/emission mismatch");
					p.finished = true;
				}
			} else if (!next.bytes.empty())
				p.pending = next.bytes;
			else
				return;
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
bool DeltaEncoder::counted() const noexcept { return impl_ && impl_->emitting && !impl_->failed; }
std::uint64_t DeltaEncoder::encoded_length() const {
	if (!counted())
		throw std::logic_error("delta encoder count incomplete");
	return impl_->length;
}
UpdateKind DeltaEncoder::kind() const {
	if (!counted())
		throw std::logic_error("delta encoder count incomplete");
	return impl_->counts.transactions ? UpdateKind::Delta : UpdateKind::Progress;
}
bool DeltaEncoder::done() const noexcept {
	return impl_ && impl_->finished && !impl_->ready && !impl_->failed;
}
bool DeltaEncoder::chunk_ready() const noexcept { return impl_ && impl_->ready && !impl_->failed; }
std::shared_ptr<const OwnedBuffer> DeltaEncoder::take_chunk() {
	if (!chunk_ready())
		throw std::logic_error("delta encoder chunk unavailable");
	impl_->ready = false;
	return std::exchange(impl_->output, {});
}
bool DeltaEncoder::discard(WorkBudget &budget) noexcept {
	auto &p = *impl_;
	p.failed = true;
	p.pending = {};
	if (p.source && !p.source->discard(budget))
		return false;
	if (p.output) {
		if (!budget.tokens)
			return false;
		if (!p.disposal)
			p.disposal = p.output->storage_capacity();
		const auto n = std::min(p.disposal, budget.bytes);
		p.disposal -= n;
		budget.bytes -= n;
		if (p.disposal)
			return false;
		--budget.tokens;
		p.output.reset();
	}
	p.source.reset();
	return true;
}
} // namespace detent::distribution
