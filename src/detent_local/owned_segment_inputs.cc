#include "owned_segment_inputs.hh"
#include <algorithm>
#include <cstring>
#include <vector>
namespace detent::local::detail {
namespace d = distribution;
struct OwnedSegmentInputs::State final : d::RetirementNode {
	std::vector<SegmentInput, d::StorageAllocator<SegmentInput>> inputs;
	std::vector<std::shared_ptr<const d::OwnedBuffer>,
				d::StorageAllocator<std::shared_ptr<const d::OwnedBuffer>>>
		buffers;
	std::shared_ptr<d::StorageLedger> account;
	StorageIdentity storage;
	std::size_t capacity, position = 0, moving = 0, charge = 0;
	std::uint64_t maximum, physical = 0;
	SegmentInput pending;
	std::shared_ptr<const d::OwnedBuffer> buffer;
	bool busy = false, frozen = false;
	State(std::shared_ptr<d::StorageLedger> account, StorageIdentity storage, std::size_t capacity,
		  std::uint64_t maximum)
		: inputs(d::StorageAllocator<SegmentInput>(account)),
		  buffers(d::StorageAllocator<std::shared_ptr<const d::OwnedBuffer>>(account)),
		  account(std::move(account)), storage(storage), capacity(capacity), maximum(maximum) {
		if (capacity > inputs.max_size() || capacity > buffers.max_size())
			throw std::length_error("owned segment input capacity exceeded");
		inputs.reserve(capacity);
		buffers.reserve(capacity);
	}
	auto find(const StorageIdentity &id) const {
		return std::lower_bound(inputs.begin(), inputs.end(), id, [](const auto &row, const auto &key) {
			return std::memcmp(row.ownership.segment.data(), key.data(), key.size()) < 0;
		});
	}
	bool retire_step() noexcept override {
		buffer.reset();
		for (unsigned count = 0; count < 16 && !buffers.empty(); ++count) {
			buffers.pop_back();
			inputs.pop_back();
		}
		return buffers.empty();
	}
};
OwnedSegmentInputs::OwnedSegmentInputs(d::RetirementFactory factory,
									   std::shared_ptr<d::StorageLedger> account, StorageIdentity storage,
									   std::size_t segments, std::uint64_t bytes) {
	state_ = factory.make<State>(account, storage, segments, bytes);
	using Handle = d::RetirementHandle<State>;
	pin_ = std::allocate_shared<Handle>(d::StorageAllocator<Handle>(account), state_);
}
OwnedSegmentInputs::~OwnedSegmentInputs() = default;
OwnedSegmentInputs::OwnedSegmentInputs(OwnedSegmentInputs &&) noexcept = default;
bool OwnedSegmentInputs::busy() const noexcept { return state_ && state_->busy; }
void OwnedSegmentInputs::freeze() {
	if (!state_ || busy())
		throw std::logic_error("owned segment inputs incomplete");
	state_->frozen = true;
}
void OwnedSegmentInputs::begin(SegmentOwnership ownership, std::shared_ptr<const d::OwnedBuffer> buffer) {
	if (!state_ || busy() || state_->frozen)
		throw std::logic_error("owned segment insertion unavailable");
	auto &s = *state_;
	encode_segment_ownership(ownership);
	if (!buffer || !buffer->sealed() || buffer->account() != s.account || ownership.storage != s.storage ||
		!ownership.length || ownership.length != buffer->size() || ownership.length > paged::segment_bytes ||
		ownership.length > s.maximum - s.physical || s.inputs.size() == s.capacity)
		throw std::invalid_argument("invalid owned segment input");
	const auto found = s.find(ownership.segment);
	if (found != s.inputs.end() && found->ownership.segment == ownership.segment)
		throw std::invalid_argument("duplicate owned segment input");
	s.position = found - s.inputs.begin();
	s.moving = s.inputs.size();
	s.pending = {ownership, {buffer->data(), buffer->size()}};
	s.buffer = std::move(buffer);
	s.inputs.emplace_back();
	s.buffers.emplace_back();
	s.busy = true;
}
void OwnedSegmentInputs::step(ValidationBudget &budget) {
	if (!state_)
		throw std::logic_error("owned segment inputs moved");
	auto &s = *state_;
	while (s.busy && budget.rows) {
		if (!s.charge)
			s.charge = sizeof(SegmentInput) + sizeof(s.buffer);
		const auto count = std::min(s.charge, budget.bytes);
		s.charge -= count;
		budget.bytes -= count;
		if (s.charge)
			return;
		--budget.rows;
		if (s.moving > s.position) {
			s.inputs[s.moving] = s.inputs[s.moving - 1];
			s.buffers[s.moving] = std::move(s.buffers[s.moving - 1]);
			--s.moving;
		} else {
			s.inputs[s.position] = s.pending;
			s.buffers[s.position] = std::move(s.buffer);
			s.physical += s.pending.ownership.length;
			s.busy = false;
		}
	}
}
std::span<const SegmentInput> OwnedSegmentInputs::inputs() const {
	if (!state_ || busy())
		throw std::logic_error("owned segment inputs incomplete");
	return state_->inputs;
}
std::shared_ptr<const void> OwnedSegmentInputs::owner() const {
	inputs();
	return std::shared_ptr<const void>(pin_, state_.get());
}
std::optional<SegmentInput> OwnedSegmentInputs::lookup(const void *owner, const StorageIdentity &id) {
	const auto &s = *static_cast<const State *>(owner);
	if (s.busy)
		throw std::logic_error("owned segment inputs incomplete");
	const auto found = s.find(id);
	if (found == s.inputs.end() || found->ownership.segment != id)
		return {};
	return *found;
}
} // namespace detent::local::detail
