#include "delivery_export.hh"
namespace detent::authority {
namespace d = distribution;
struct DeliveryExport::Impl {
	std::shared_ptr<const AuthorizedScope> scope;
	std::shared_ptr<ChangeHistory> history;
	std::shared_ptr<ExportCapture> capture;
	std::optional<d::Cursor> base;
	d::Identity update;
	d::Limits limits;
	bool compress, fallback = false, failed = false, retiring = false, finished = false, encoding = false;
	std::optional<DeltaExportEncoder> delta;
	std::optional<SnapshotExportEncoder> snapshot;
	std::optional<d::OwnedFragmentEncoder> frames;
	std::uint64_t total = 0, emitted = 0;
	std::size_t snapshot_disposal = d::fragment_bytes;
	d::UpdateKind type = d::UpdateKind::Snapshot;
	Impl(const State &state, std::shared_ptr<const AuthorizedScope> authorized,
		 std::shared_ptr<ChangeHistory> changes, std::optional<d::Cursor> from, d::Identity id, bool coding,
		 d::Limits bounds)
		: scope(std::move(authorized)), history(std::move(changes)), base(from), update(id),
		  limits(std::move(bounds)), compress(coding) {
		if (!scope || update == d::Identity{} ||
			(limits.storage_account && limits.storage_account != scope->storage_account()) ||
			(history && history->storage_account() != scope->storage_account()))
			throw std::invalid_argument("delivery export admission mismatch");
		limits.storage_account = scope->storage_account();
		capture = state.capture(scope);
	}
};
DeliveryExport::DeliveryExport(const State &state, std::shared_ptr<const AuthorizedScope> scope,
							   std::shared_ptr<ChangeHistory> history, std::optional<d::Cursor> base,
							   d::Identity update, bool compress, d::Limits limits)
	: impl_([&] {
		  if (!scope)
			  throw std::invalid_argument("delivery scope missing");
		  return d::make_storage_owner<Impl>(scope->storage_account(), std::malloc, std::free, state, scope,
											 std::move(history), base, update, compress, std::move(limits));
	  }()) {}
DeliveryExport::~DeliveryExport() = default;
void DeliveryExport::step(d::WorkBudget &budget) {
	auto &p = *impl_;
	if (p.failed || p.retiring)
		throw std::logic_error("delivery export inactive");
	if (p.finished || (p.frames && p.frames->ready()) || !budget.tokens || !budget.bytes)
		return;
	try {
		if (!p.delta && !p.snapshot && !p.fallback) {
			--budget.tokens;
			const auto &target = p.capture->cursor();
			Context context{target.cluster, target.group, target.restore_epoch};
			if (p.history && p.base && p.base->same_identity(target) && p.base->revision < target.revision &&
				p.history->covers(context, p.base->revision, target.revision)) {
				try {
					p.delta.emplace(p.history, p.scope, context, *p.base, target, p.limits);
				} catch (const std::invalid_argument &) {
					p.fallback = true;
				} catch (const std::length_error &) {
					p.fallback = true;
				}
			} else
				p.fallback = true;
			p.history.reset();
		}
		if (p.fallback) {
			if (p.delta) {
				if (!p.delta->discard(budget))
					return;
				p.delta.reset();
			}
			if (!budget.tokens || !budget.bytes)
				return;
			--budget.tokens;
			p.snapshot.emplace(p.capture, p.limits);
			p.fallback = false;
		}
		if (!p.frames) {
			if (p.delta) {
				try {
					p.delta->step(budget);
				} catch (const std::invalid_argument &) {
					p.fallback = true;
					return;
				} catch (const std::length_error &) {
					p.fallback = true;
					return;
				}
				if (!p.delta->counted())
					return;
				p.total = p.delta->encoded_length();
				p.type = p.delta->kind();
			} else {
				p.snapshot->step(budget);
				if (!p.snapshot->counted())
					return;
				p.total = p.snapshot->encoded_length();
			}
			if (!budget.tokens)
				return;
			--budget.tokens;
			p.frames.emplace(p.type, p.update, p.total, p.compress, p.scope->storage_account());
		}
		// The frame encoder owns its input until take_frame(). Never offer a
		// second input while the first is being encoded or awaiting transfer.
		if (!p.encoding && p.emitted < p.total) {
			bool ready = p.delta ? p.delta->chunk_ready() : p.snapshot->chunk_ready();
			if (!ready) {
				if (p.delta)
					p.delta->step(budget);
				else
					p.snapshot->step(budget);
				ready = p.delta ? p.delta->chunk_ready() : p.snapshot->chunk_ready();
			}
			if (ready) {
				auto input = p.delta ? p.delta->take_chunk() : p.snapshot->take_chunk();
				const auto next = p.emitted + input->size();
				p.frames->begin(std::move(input), next == p.total);
				p.emitted = next;
				p.encoding = true;
			}
		}
		p.frames->step(budget);
	} catch (...) {
		p.failed = true;
		throw;
	}
}
bool DeliveryExport::counted() const noexcept { return impl_->frames.has_value(); }
std::uint64_t DeliveryExport::encoded_length() const {
	if (!counted())
		throw std::logic_error("delivery export not counted");
	return impl_->total;
}
d::UpdateKind DeliveryExport::kind() const {
	if (!counted())
		throw std::logic_error("delivery export not counted");
	return impl_->type;
}
const d::Cursor &DeliveryExport::target() const noexcept { return impl_->capture->cursor(); }
bool DeliveryExport::frame_ready() const noexcept {
	return impl_->frames && impl_->frames->ready() && !impl_->retiring;
}
std::shared_ptr<const d::OwnedBuffer> DeliveryExport::take_frame() {
	if (!frame_ready())
		throw std::logic_error("delivery frame not ready");
	auto &p = *impl_;
	auto result = p.frames->take();
	p.encoding = false;
	p.finished = p.emitted == p.total;
	return result;
}
bool DeliveryExport::done() const noexcept { return impl_->finished; }
bool DeliveryExport::discard(d::WorkBudget &bytes, CommandWorkBudget &roots) noexcept {
	auto &p = *impl_;
	p.retiring = true;
	if (p.frames) {
		if (!p.frames->discard(bytes))
			return false;
		p.frames.reset();
	}
	if (p.delta) {
		if (!p.delta->discard(bytes))
			return false;
		p.delta.reset();
	}
	if (p.snapshot) {
		if (!bytes.tokens)
			return false;
		const auto n = std::min(p.snapshot_disposal, bytes.bytes);
		p.snapshot_disposal -= n;
		bytes.bytes -= n;
		if (p.snapshot_disposal)
			return false;
		--bytes.tokens;
		p.snapshot->discard();
		p.snapshot.reset();
	}
	p.history.reset();
	p.capture->retire();
	p.capture->collect(roots);
	return !p.capture->reclamation_pending();
}
} // namespace detent::authority
