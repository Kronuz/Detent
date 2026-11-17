#include "frames_export.hh"
#include <algorithm>

namespace detent::authority {
namespace d = distribution;

FramesExport::FramesExport(std::vector<FramesRequest> requests, std::optional<d::DirectoryUpdate> directory,
						   d::Identity update, bool compress, std::shared_ptr<d::StorageLedger> account,
						   std::size_t maximum_bytes)
	: owned_(std::move(requests)), account_(std::move(account)), maximum_(maximum_bytes), update_(update),
	  compress_(compress) {
	requests_.reserve(owned_.size());
	for (const auto &request : owned_)
		requests_.push_back({request.name, request.history.get(), request.store, request.dropped});
	encoder_.emplace(directory, requests_, account_);
	empty_ = !directory || directory->entries.empty();
	for (const auto &answer : encoder_->answers())
		empty_ = empty_ && (answer.kind == d::AnswerKind::Current || answer.kind == d::AnswerKind::Ahead);
}
FramesExport::~FramesExport() = default;

FramesFailure frames_failure(const std::exception &error) noexcept {
	if (dynamic_cast<const FramesBehind *>(&error))
		return {503, "{\"error\":\"source_behind\"}"};
	return {503, "{\"error\":\"distribution_unavailable\"}"};
}

FramesPlan plan_frames(std::span<const Followed> follows, const std::optional<d::DirectoryCursor> &all,
					   const d::NamespaceGrants &grants, const State &state, const Identity &cluster,
					   const State &sealed, const history::Table &table, history::Files &files,
					   const ChangeHistory &ring) {
	FramesPlan plan;
	auto request = [&](const std::string &name, const std::optional<d::NamespaceCursor> &store) {
		if (auto frozen = AuthorityHistory::freeze(name, cluster, state, sealed, table, files, ring)) {
			plan.requests.push_back({name, std::move(frozen), store, std::nullopt});
			return;
		}
		const auto *entry = state.namespace_entry(name);
		if (!entry)
			return; // Not created on this voter.
		if (entry->live)
			throw FramesBehind("namespace history is not at the sealed state");
		// A store above the drop is at a later incarnation this voter has not applied: it gets no frame.
		if (store && !(store->cluster == cluster && store->position > entry->position))
			plan.requests.push_back({name, nullptr, store, d::NamespaceCursor{cluster, entry->incarnation, entry->position}});
	};
	for (const auto &[name, store] : follows)
		request(name, store);
	if (all) {
		d::DirectoryUpdate update;
		update.from = all->cluster == cluster ? all->position : 0;
		update.to = {cluster, state.directory_position()};
		if (update.from < update.to.position) {
			state.for_each_directory_entry(update.from, [&](std::uint64_t, const std::string &name, const Namespace &entry) {
				if (!grants.allows(name))
					return;
				update.entries.push_back({name, entry.incarnation, entry.live});
				const bool followed = std::any_of(follows.begin(), follows.end(),
												  [&](const auto &f) { return f.first == name; });
				if (entry.live && !followed)
					request(name, std::nullopt);
			});
			plan.directory = std::move(update);
		}
	}
	std::sort(plan.requests.begin(), plan.requests.end(), [](const auto &a, const auto &b) { return a.name < b.name; });
	return plan;
}

void FramesExport::seal_chunk() {
	chunk_->seal_prefix();
	chunks_.push_back(std::move(chunk_));
	chunk_.reset();
}

void FramesExport::step(d::WorkBudget &budget) {
	while (!counted_ && budget.tokens && budget.bytes) {
		--budget.tokens;
		const auto ask = std::min<std::size_t>(budget.bytes, 16384);
		const bool finished = encoder_->step(scratch_, ask);
		budget.bytes -= std::min(budget.bytes, scratch_.size());
		std::string_view pending(scratch_);
		while (!pending.empty()) {
			if (pending.size() > maximum_ - total_)
				throw std::length_error("session answer above the delivery bound");
			if (!chunk_)
				chunk_ = d::make_owned_buffer(account_, d::fragment_bytes);
			const auto n = std::min(pending.size(), d::fragment_bytes - chunk_->size());
			chunk_->append(pending.substr(0, n));
			pending.remove_prefix(n);
			total_ += n;
			if (chunk_->size() == d::fragment_bytes)
				seal_chunk();
		}
		scratch_.clear();
		if (finished) {
			if (chunk_ && !chunk_->empty())
				seal_chunk();
			chunk_.reset();
			counted_ = true;
			encoder_.reset();
			owned_.clear(); // The histories' frozen roots go with the encoder.
			fragments_.emplace(d::UpdateKind::Frames, update_, total_, compress_, account_);
		}
	}
	while (counted_ && !fragments_->ready() && budget.tokens && budget.bytes) {
		if (!active_) {
			if (chunks_.empty())
				break;
			auto next = std::move(chunks_.front());
			chunks_.pop_front();
			offered_ += next->size();
			fragments_->begin(std::move(next), offered_ == total_);
			active_ = true;
		}
		fragments_->step(budget);
		if (!fragments_->ready())
			break;
	}
}
bool FramesExport::frame_ready() const noexcept { return fragments_ && fragments_->ready(); }
std::shared_ptr<const d::OwnedBuffer> FramesExport::take_frame() {
	auto frame = fragments_->take();
	active_ = false;
	done_ = offered_ == total_ && chunks_.empty();
	return frame;
}
bool FramesExport::done() const noexcept { return done_; }
} // namespace detent::authority
