#include "detent_distribution/update_cache.hh"
#include <algorithm>
#include <limits>

namespace detent::distribution {
UpdateCache::UpdateCache(Identity incarnation, std::size_t edges, bool retain_snapshot)
	: incarnation_(incarnation), limit_(edges), retain_snapshot_(retain_snapshot) {
	if (!edges || edges > maximum_edges ||
		std::all_of(incarnation.begin(), incarnation.end(), [](char value) { return value == 0; }))
		throw std::invalid_argument("invalid update cache configuration");
}
void UpdateCache::gap() {
	if (lineage_ == std::numeric_limits<std::uint64_t>::max())
		throw std::length_error("update cache lineage exhausted");
	++lineage_;
	cleanup_ = credit_ = 0;
	snapshot_.reset();
}
void UpdateCache::select_publication(Cursor selected, RetirementHandle<EncodedUpdate> update) {
	if (fenced_)
		throw std::logic_error("update cache fenced");
	if (head_ && (!head_->same_identity(selected) || selected.revision < head_->revision))
		throw std::invalid_argument("update cache selection identity mismatch");
	if (update && (!update->sealed() || update->target() != selected))
		throw std::invalid_argument("update cache publication mismatch");
	if (update && update->kind() != UpdateKind::Snapshot &&
		(!head_ || update->base() != *head_ || selected.revision <= head_->revision))
		throw std::invalid_argument("update cache edge mismatch");
	if (head_ && selected == *head_ && !update)
		return;
	if (!update || update->kind() == UpdateKind::Snapshot) {
		gap();
		head_ = selected;
		if (update)
			snapshot_ = std::move(update);
		return;
	}
	edges_[next_] = {std::move(update), lineage_};
	next_ = (next_ + 1) % limit_;
	head_ = selected;
	if (!retain_snapshot_ || (snapshot_ && !covers_to_head(snapshot_->target())))
		snapshot_.reset();
}
void UpdateCache::install_snapshot(RetirementHandle<EncodedUpdate> update) {
	if (fenced_ || !head_ || !update || !update->sealed() || update->kind() != UpdateKind::Snapshot ||
		(retain_snapshot_ ? !covers_to_head(update->target()) : update->target() != *head_))
		throw std::invalid_argument("update cache snapshot mismatch");
	snapshot_ = std::move(update);
}
CacheOffer UpdateCache::offer(std::optional<Cursor> base, bool force_snapshot) const {
	if (fenced_)
		return {CacheDecision::Fenced, {}, marker()};
	if (!head_)
		return {CacheDecision::Busy, {}, marker()};
	if (base && !base->same_identity(*head_))
		return {CacheDecision::IdentityChanged, {}, marker()};
	if (base && base->revision > head_->revision)
		return {CacheDecision::SourceBehind, {}, marker()};
	if (force_snapshot || !base) {
		if (snapshot_ && (!base || base->revision <= snapshot_->target().revision) &&
			covers_to_head(snapshot_->target()))
			return {CacheDecision::Update, snapshot_, marker()};
		return {CacheDecision::Busy, {}, marker()};
	}
	if (*base == *head_)
		return {CacheDecision::Idle, {}, marker()};
	for (std::size_t i = 0; i < limit_; ++i) {
		const auto &edge = edges_[i];
		if (edge.lineage == lineage_ && edge.update && edge.update->base() == *base)
			return {CacheDecision::Update, edge.update, marker()};
	}
	return {CacheDecision::SnapshotRequired, {}, marker()};
}
bool UpdateCache::covers(CacheMarker mark, const Cursor &base) const noexcept {
	if (fenced_ || mark != marker() || !head_ || !base.same_identity(*head_))
		return false;
	if (base == *head_)
		return true;
	for (std::size_t i = 0; i < limit_; ++i)
		if (edges_[i].lineage == lineage_ && edges_[i].update && edges_[i].update->base() == base)
			return true;
	return false;
}
std::optional<Cursor> UpdateCache::snapshot_frontier() const noexcept {
	return snapshot_ ? std::optional(snapshot_->target()) : std::nullopt;
}
bool UpdateCache::covers_to_head(const Cursor &base) const noexcept {
	return edges_to_head(base).has_value();
}
std::optional<std::size_t> UpdateCache::edges_to_head(const Cursor &base) const noexcept {
	return walk_to_head(base, {});
}
std::optional<std::size_t> UpdateCache::walk_to_head(
	const Cursor &base, std::span<RetirementHandle<EncodedUpdate>> output) const noexcept {
	if (fenced_ || !head_ || !base.same_identity(*head_) || base.revision > head_->revision)
		return std::nullopt;
	auto cursor = base;
	std::size_t count = 0;
	if (cursor == *head_) return count;
	for (std::size_t n = 0; n < limit_; ++n) {
		const auto &edge = edges_[(next_ + n) % limit_];
		if (edge.lineage != lineage_ || !edge.update || edge.update->target().revision <= base.revision)
			continue;
		if (edge.update->base() != cursor || edge.update->target().revision <= cursor.revision)
			return std::nullopt;
		if (!output.empty()) {
			if (count == output.size()) return std::nullopt;
			output[count] = edge.update;
		}
		++count;
		cursor = edge.update->target();
		if (cursor == *head_) return count;
	}
	return std::nullopt;
}
SnapshotPath UpdateCache::capture_snapshot_path(std::optional<Cursor> minimum,
	std::span<RetirementHandle<EncodedUpdate>> output) const {
	const auto offer = this->offer(minimum, true);
	if (offer.decision != CacheDecision::Update)
		return {offer.decision, {}, offer.marker, 0};
	const auto edges = edges_to_head(offer.update->target());
	if (!edges || output.size() < *edges + 1)
		return {};
	output[0] = offer.update;
	if (*edges) (void)walk_to_head(offer.update->target(), output.subspan(1));
	return {CacheDecision::Update, *head_, marker(), *edges + 1};
}
void UpdateCache::fence() noexcept {
	fenced_ = true;
	cleanup_ = credit_ = 0;
	snapshot_.reset();
}
bool UpdateCache::release_stale(WorkBudget &budget) noexcept {
	while (cleanup_ < limit_) {
		const auto amount = std::min(budget.bytes, sizeof(Edge) - credit_);
		budget.bytes -= amount;
		credit_ += amount;
		if (credit_ != sizeof(Edge) || !budget.tokens)
			return false;
		credit_ = 0;
		--budget.tokens;
		auto &edge = edges_[cleanup_++];
		if (fenced_ || edge.lineage != lineage_)
			edge.update.reset();
	}
	cleanup_ = 0;
	return true;
}
std::size_t UpdateCache::retained_edges() const noexcept {
	std::size_t count = 0;
	for (std::size_t i = 0; i < limit_; ++i)
		count += edges_[i].lineage == lineage_ && static_cast<bool>(edges_[i].update);
	return count;
}
} // namespace detent::distribution
