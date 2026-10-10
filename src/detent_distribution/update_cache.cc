#include "detent_distribution/update_cache.hh"
#include <algorithm>
#include <limits>

namespace detent::distribution {
UpdateCache::UpdateCache(Identity incarnation, std::size_t edges) : incarnation_(incarnation), limit_(edges) {
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
	// An older snapshot cannot satisfy a forced reset at the selected head.
	snapshot_.reset();
}
void UpdateCache::install_snapshot(RetirementHandle<EncodedUpdate> update) {
	if (fenced_ || !head_ || !update || !update->sealed() || update->kind() != UpdateKind::Snapshot ||
		update->target() != *head_)
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
		if (snapshot_ && snapshot_->target() == *head_)
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
