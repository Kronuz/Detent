#include "detent_distribution/encoded_update.hh"
#include "detent_distribution/frame_pump.hh"
#include <optional>

namespace detent::distribution {
EncodedUpdate::EncodedUpdate(Cursor base, Cursor target, UpdateKind kind, Identity update,
							 std::uint64_t length, std::shared_ptr<StorageLedger> account)
	: base_(base), target_(target), kind_(kind), update_(update), length_(length),
	  raw_(StorageAllocator<std::shared_ptr<const OwnedBuffer>>(account)), compressed_(raw_.get_allocator()) {
	const auto fragments = length / fragment_bytes + (length % fragment_bytes != 0 || length == 0);
	if (fragments > 65536)
		throw std::length_error("encoded update fragment directory limit");
	raw_.reserve(static_cast<std::size_t>(fragments));
	compressed_.reserve(static_cast<std::size_t>(fragments));
}
std::shared_ptr<const OwnedBuffer> EncodedUpdate::frame(std::size_t ordinal, bool compressed) const {
	if (!sealed_)
		throw std::logic_error("unsealed encoded update");
	return (compressed ? compressed_ : raw_).at(ordinal);
}
bool EncodedUpdate::retire_step() noexcept {
	if (!raw_.empty()) {
		raw_.pop_back();
		return false;
	}
	if (!compressed_.empty()) {
		compressed_.pop_back();
		return false;
	}
	return true;
}
struct UpdateStage::Impl {
	std::shared_ptr<StorageLedger> account;
	Cursor base, target;
	Identity update;
	std::optional<SnapshotEncoder> snapshot;
	std::optional<DeltaEncoder> delta;
	std::optional<FramePump> raw, compressed;
	RetirementHandle<EncodedUpdate> output;
	std::shared_ptr<const OwnedBuffer> chunk;
	std::size_t credit = 0;
	bool initialized = false, finishing = false, finished = false, retiring = false, failed = false,
		 raw_taken = false, compressed_taken = false;
	Impl(RetirementDomain &owner, std::shared_ptr<SnapshotSource> source, Identity identity, Limits limits)
		: account(owner.storage_account()), base(source->cursor()), target(base), update(identity) {
		if (source->storage_account() != account)
			throw std::invalid_argument("snapshot stage account mismatch");
		output = owner.make<EncodedUpdate>(base, target, UpdateKind::Snapshot, update, 0, account);
		snapshot.emplace(std::move(source), std::move(limits));
	}
	Impl(RetirementDomain &owner, std::shared_ptr<DeltaSource> source, Identity identity, Limits limits)
		: account(owner.storage_account()), base(source->base()), target(source->target()), update(identity) {
		if (source->storage_account() != account)
			throw std::invalid_argument("delta stage account mismatch");
		output = owner.make<EncodedUpdate>(base, target, UpdateKind::Delta, update, 0, account);
		delta.emplace(std::move(source), std::move(limits));
	}
	bool charge(WorkBudget &budget, std::size_t bytes) noexcept {
		const auto amount = std::min(budget.bytes, bytes - credit);
		budget.bytes -= amount;
		credit += amount;
		if (credit != bytes || !budget.tokens)
			return false;
		--budget.tokens;
		credit = 0;
		return true;
	}
	bool counted() const noexcept { return snapshot ? snapshot->counted() : delta->counted(); }
	bool source_done() const noexcept { return snapshot ? snapshot->done() : delta->done(); }
	bool chunk_ready() const noexcept { return snapshot ? snapshot->chunk_ready() : delta->chunk_ready(); }
	void source_step(WorkBudget &budget) { snapshot ? snapshot->step(budget) : delta->step(budget); }
	bool clean(WorkBudget &budget) noexcept {
		if (raw) {
			if (!raw->discard(budget))
				return false;
			raw.reset();
		}
		if (compressed) {
			if (!compressed->discard(budget))
				return false;
			compressed.reset();
		}
		if (snapshot) {
			if (!snapshot->discard(budget))
				return false;
			snapshot.reset();
		}
		if (delta) {
			if (!delta->discard(budget))
				return false;
			delta.reset();
		}
		chunk.reset();
		return true;
	}
	void step(WorkBudget &budget) {
		if (finished || retiring || failed)
			throw std::logic_error("encoded update stage unavailable");
		if (!initialized) {
			if (!counted()) {
				source_step(budget);
				return;
			}
			if (!charge(budget, 512))
				return;
			const auto length = snapshot ? snapshot->encoded_length() : delta->encoded_length();
			const auto kind = snapshot ? UpdateKind::Snapshot : delta->kind();
			const auto fragments = length / fragment_bytes + (length % fragment_bytes != 0 || length == 0);
			if (fragments > 65536)
				throw std::length_error("encoded update fragment directory limit");
			output->raw_.reserve(static_cast<std::size_t>(fragments));
			output->compressed_.reserve(static_cast<std::size_t>(fragments));
			output->length_ = length;
			output->kind_ = kind;
			raw.emplace(kind, update, length, false, account);
			compressed.emplace(kind, update, length, true, account);
			initialized = true;
			return;
		}
		if (finishing || (raw->done() && compressed->done())) {
			if (!finishing && !source_done())
				throw std::logic_error("encoded update source length mismatch");
			finishing = true;
			if (clean(budget)) {
				output->sealed_ = true;
				finished = true;
			}
			return;
		}
		if (!chunk) {
			if (!chunk_ready()) {
				source_step(budget);
				return;
			}
			chunk = snapshot ? snapshot->take_chunk() : delta->take_chunk();
			raw->begin(chunk);
			compressed->begin(chunk);
			raw_taken = compressed_taken = false;
		}
		if (!raw_taken) {
			if (!raw->ready()) {
				raw->step(budget);
				return;
			}
			if (!charge(budget, sizeof(std::shared_ptr<const OwnedBuffer>)))
				return;
			output->raw_.push_back(raw->take());
			raw_taken = true;
		}
		if (!compressed_taken) {
			if (!compressed->ready()) {
				compressed->step(budget);
				return;
			}
			if (!charge(budget, sizeof(std::shared_ptr<const OwnedBuffer>)))
				return;
			output->compressed_.push_back(compressed->take());
			compressed_taken = true;
		}
		chunk.reset();
	}
};
UpdateStage::UpdateStage(RetirementDomain &domain, std::shared_ptr<SnapshotSource> source, Identity identity,
						 Limits limits) {
	if (!source)
		throw std::invalid_argument("missing snapshot stage source");
	impl_ = make_storage_owner<Impl>(domain.storage_account(), std::malloc, std::free, domain,
									 std::move(source), identity, std::move(limits));
}
UpdateStage::UpdateStage(RetirementDomain &domain, std::shared_ptr<DeltaSource> source, Identity identity,
						 Limits limits) {
	if (!source)
		throw std::invalid_argument("missing delta stage source");
	impl_ = make_storage_owner<Impl>(domain.storage_account(), std::malloc, std::free, domain,
									 std::move(source), identity, std::move(limits));
}
UpdateStage::~UpdateStage() = default;
void UpdateStage::step(WorkBudget &budget) {
	if (!impl_)
		throw std::logic_error("encoded update stage consumed");
	try {
		impl_->step(budget);
	} catch (...) {
		impl_->failed = true;
		throw;
	}
}
bool UpdateStage::done() const noexcept { return impl_ && impl_->finished; }
RetirementHandle<EncodedUpdate> UpdateStage::take() {
	if (!done())
		throw std::logic_error("encoded update stage unfinished");
	auto result = std::move(impl_->output);
	impl_.reset();
	return result;
}
bool UpdateStage::abort(WorkBudget &budget) noexcept {
	if (!impl_)
		return true;
	impl_->retiring = true;
	if (!impl_->clean(budget))
		return false;
	impl_.reset();
	return true;
}
bool UpdateStage::retire_step() noexcept {
	WorkBudget budget;
	return abort(budget);
}
} // namespace detent::distribution
