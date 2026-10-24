#include "detent_relay/bundle.hh"
#include "snapshot_source.hh"
#include "state_equivalence_proof.hh"
#include <algorithm>
#include <limits>

namespace detent::relay {
namespace d = distribution;
bool BootstrapPlan::covers(const d::Cursor &base) const noexcept {
	if (!base.same_identity(head) || base.revision >= head.revision) return false;
	for (std::size_t i = position; i < count; ++i)
		if (updates[i] && updates[i]->kind() != d::UpdateKind::Snapshot && updates[i]->base() == base)
			return true;
	return false;
}
void BootstrapPlan::accepted(std::uint64_t window, std::optional<d::Cursor> base) {
	if (!offered_window || window != offered_window) return;
	if (position == count || !base || updates[position]->target() != *base)
		throw std::logic_error("relay bootstrap acknowledgment mismatch");
	updates[position++].reset();
	offered_window = 0;
}
bool BootstrapPlan::retire_step() noexcept {
	for (unsigned n = 0; n != 16 && position != count; ++n)
		updates[position++].reset();
	return position == count;
}
Bundle::Bundle(BundleOptions options, d::RetirementDomain &domain)
	: options_(std::move(options)), domain_(domain), cache_(options_.incarnation, options_.retained_edges, true) {
	if (options_.namespaces > options_.encoding.namespaces)
		throw std::invalid_argument("relay bundle namespace limit");
	options_.encoding.storage_account = domain.storage_account();
}
d::Identity Bundle::next_identity() {
	for (unsigned attempt = 0; attempt < 2; ++attempt) {
		if (sequence_ == std::numeric_limits<std::uint64_t>::max())
			throw std::length_error("relay update identity exhausted");
		auto result = options_.incarnation;
		const auto sequence = ++sequence_;
		for (unsigned i = 0; i < 8; ++i)
			result[15 - i] ^= static_cast<char>(sequence >> (i * 8));
		if (std::any_of(result.begin(), result.end(), [](char value) { return value != 0; }))
			return result;
	}
	throw std::length_error("relay update identity unavailable");
}
void Bundle::authenticated() noexcept {
	if (!diagnostic_.fenced)
		diagnostic_.authenticated = true;
}
bool Bundle::ready() const noexcept {
	return !diagnostic_.fenced && diagnostic_.authenticated && head_.has_value() &&
		   head_->descriptor().cursor.same_identity(options_.configured);
}
void Bundle::refused() noexcept {
	++diagnostic_.refusals;
	diagnostic_.error = std::current_exception();
}
void Bundle::begin_stage(const delivery::ReceivedUpdate &update) {
	if (diagnostic_.fenced || stage_ || !update.target.same_identity(options_.configured))
		throw std::logic_error("relay bundle staging state mismatch");
	token_.reset();
	candidate_.reset();
	stage_failed_ = false;
	try {
		const auto identity = next_identity();
		if (update.snapshot)
			stage_ = domain_.make<d::UpdateStage>(
				domain_, d::make_snapshot_source(update.snapshot, domain_.storage_account()), identity,
				options_.encoding);
		else if (update.delta)
			stage_ = domain_.make<d::UpdateStage>(
				domain_, d::make_delta_source(update.delta, options_.namespaces, domain_.storage_account()),
				identity, options_.encoding);
		else
			throw std::invalid_argument("relay bundle update missing validated input");
	} catch (const d::StoragePressure &) {
		refused();
		stage_failed_ = true;
	} catch (const std::bad_alloc &) {
		refused();
		stage_failed_ = true;
	}
}
bool Bundle::stage_step(d::WorkBudget &budget) {
	if (!stage_)
		return true;
	if (stage_failed_)
		return release_stage(budget);
	try {
		stage_->step(budget);
		if (!stage_->done())
			return false;
		token_ = stage_->take();
		stage_.reset();
		++diagnostic_.staged;
		return true;
	} catch (const d::StoragePressure &) {
		refused();
	} catch (const std::length_error &) {
		refused();
	} catch (const std::bad_alloc &) {
		refused();
	}
	stage_failed_ = true;
	return release_stage(budget);
}
void Bundle::bind_candidate(const local::Descriptor &descriptor,
							local::DirectoryIdentity directory) noexcept {
	candidate_ = descriptor;
	candidate_directory_ = directory;
}
bool Bundle::release_stage(d::WorkBudget &budget) noexcept {
	if (stage_ && !stage_->abort(budget))
		return false;
	stage_.reset();
	return true;
}
void Bundle::selected(const local::RecoveredGeneration &generation) {
	const auto &descriptor = generation.descriptor();
	if (!descriptor.cursor.same_identity(options_.configured)) {
		// A configured identity reset can recover the prior head during startup.
		token_.reset();
		candidate_.reset();
		return;
	}
	if (head_ && head_->directory_identity() != generation.directory_identity())
		throw std::invalid_argument("relay durable directory changed");
	if (head_ && head_->descriptor().cursor == descriptor.cursor &&
		(head_->descriptor().length != descriptor.length ||
		 head_->descriptor().checksum != descriptor.checksum ||
		 head_->descriptor().profile != descriptor.profile))
		throw std::invalid_argument("relay same-cursor state conflict");
	const bool matched = token_ && candidate_ && *candidate_ == descriptor &&
						 candidate_directory_ == generation.directory_identity() &&
						 token_->target() == descriptor.cursor;
	if (matched)
		cache_.select_publication(descriptor.cursor, std::move(token_));
	else {
		const bool changed = !cache_.selected() || *cache_.selected() != descriptor.cursor;
		cache_.select_publication(descriptor.cursor);
		if (changed)
			++diagnostic_.gaps;
		token_.reset();
	}
	if (snapshot_stage_ && snapshot_base_ &&
		!cache_.covers_to_head(snapshot_base_->cursor))
		snapshot_failed_ = true;
	candidate_.reset();
	head_ = generation;
}
void Bundle::selected_equivalent(const local::RecoveredGeneration &generation,
								 const local::detail::StateEquivalenceProof &proof) {
	if (diagnostic_.fenced || !head_ || stage_ || token_ || candidate_ || !proof.binds(*head_, generation) ||
		head_->directory_identity() != generation.directory_identity())
		throw std::invalid_argument("relay maintenance proof does not match settled physical head");
	if (snapshot_base_ && *snapshot_base_ == head_->descriptor() &&
		snapshot_directory_ == head_->directory_identity()) {
		snapshot_base_ = generation.descriptor();
		snapshot_directory_ = generation.directory_identity();
	}
	head_ = generation;
}
d::CacheOffer Bundle::offer(std::optional<d::Cursor> base, bool force_snapshot) const {
	if (!ready())
		return {diagnostic_.fenced ? d::CacheDecision::Fenced : d::CacheDecision::Busy, {}, cache_.marker()};
	return cache_.offer(base, force_snapshot);
}
BootstrapCapture Bundle::capture_bootstrap(std::optional<d::Cursor> minimum) {
	if (!ready()) return {diagnostic_.fenced ? d::CacheDecision::Fenced : d::CacheDecision::Busy, {}};
	try {
		auto plan = domain_.make<BootstrapPlan>();
		const auto captured = cache_.capture_snapshot_path(minimum, plan->updates);
		if (captured.decision != d::CacheDecision::Update) {
			if (minimum && minimum->same_identity(*cache_.selected()) &&
				minimum->revision <= cache_.selected()->revision &&
				(!requested_snapshot_ || requested_snapshot_->revision < minimum->revision))
				requested_snapshot_ = minimum;
			return {captured.decision, {}};
		}
		plan->count = captured.count;
		plan->head = captured.head;
		plan->marker = captured.marker;
		return {d::CacheDecision::Update, std::move(plan)};
	} catch (const d::StoragePressure &) { refused(); }
	catch (const std::bad_alloc &) { refused(); }
	return {};
}
bool Bundle::covers(d::CacheMarker marker, const d::Cursor &base) const noexcept {
	return ready() && cache_.covers(marker, base);
}
bool Bundle::snapshot_step(d::WorkBudget &budget) {
	if (!ready())
		return false;
	if (!snapshot_stage_) {
		const auto frontier = cache_.snapshot_frontier();
		const auto refresh_at = std::max<std::size_t>(1, options_.retained_edges / 2);
		if (frontier && (!requested_snapshot_ || requested_snapshot_->revision <= frontier->revision) &&
			cache_.edges_to_head(*frontier).value_or(options_.retained_edges) < refresh_at)
			return true;
		try {
			snapshot_base_ = head_->descriptor();
			snapshot_directory_ = head_->directory_identity();
			snapshot_failed_ = false;
			snapshot_stage_ = domain_.make<d::UpdateStage>(
				domain_, local::detail::make_snapshot_source(*head_, domain_.storage_account()),
				next_identity(), options_.encoding);
			++diagnostic_.snapshot_started;
		} catch (const d::StoragePressure &) {
			refused();
			return false;
		} catch (const std::bad_alloc &) {
			refused();
			return false;
		}
	}
	if (snapshot_failed_) {
		const auto discarded_bytes = snapshot_stage_->emitted_logical_bytes();
		if (!snapshot_stage_->abort(budget))
			return false;
		++diagnostic_.snapshot_discarded;
		diagnostic_.snapshot_discarded_logical_bytes += discarded_bytes;
		snapshot_stage_.reset();
		snapshot_base_.reset();
		return false;
	}
	try {
		snapshot_stage_->step(budget);
		if (!snapshot_stage_->done())
			return false;
		auto snapshot = snapshot_stage_->take();
		snapshot_stage_.reset();
		if (!cache_.covers_to_head(snapshot_base_->cursor) ||
			snapshot_directory_ != head_->directory_identity()) {
			++diagnostic_.snapshot_discarded;
			diagnostic_.snapshot_discarded_logical_bytes += snapshot->logical_bytes();
			snapshot_base_.reset();
			return false;
		}
		cache_.install_snapshot(std::move(snapshot));
		if (requested_snapshot_ && requested_snapshot_->revision <= snapshot_base_->cursor.revision)
			requested_snapshot_.reset();
		snapshot_base_.reset();
		++diagnostic_.snapshots;
		return true;
	} catch (const d::StoragePressure &) {
		refused();
	} catch (const std::length_error &) {
		refused();
	} catch (const std::bad_alloc &) {
		refused();
	}
	snapshot_failed_ = true;
	return false;
}
bool Bundle::cleanup_step(d::WorkBudget &budget) noexcept { return cache_.release_stale(budget); }
void Bundle::fence() noexcept {
	diagnostic_.fenced = true;
	diagnostic_.authenticated = false;
	cache_.fence();
	token_.reset();
	snapshot_stage_.reset();
	snapshot_base_.reset();
	requested_snapshot_.reset();
	head_.reset();
}
BundleStatus Bundle::status() const noexcept {
	auto result = diagnostic_;
	result.ready = ready();
	result.selected = cache_.selected();
	result.snapshot_frontier = cache_.snapshot_frontier();
	result.retained_edges = cache_.retained_edges();
	return result;
}
std::shared_ptr<Bundle> make_bundle(BundleOptions options, d::RetirementDomain &domain) {
	return std::allocate_shared<Bundle>(d::StorageAllocator<Bundle>(domain.storage_account()),
										std::move(options), domain);
}
} // namespace detent::relay
