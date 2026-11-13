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
		requests_.push_back({request.name, request.history.get(), request.store});
	encoder_.emplace(directory, requests_);
	empty_ = !directory || directory->entries.empty();
	for (const auto &answer : encoder_->answers())
		empty_ = empty_ && (answer.kind == d::AnswerKind::Current || answer.kind == d::AnswerKind::Ahead);
}
FramesExport::~FramesExport() = default;

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
