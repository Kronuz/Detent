#include "paged_graph_validator.hh"
#include "detent_local/paged_definition.hh"
#include "paged_navigation.hh"
#include "read_root.hh"
#include "segment_registry.hh"
#include <algorithm>
#include <cstring>
#include <optional>
#include <sha256.h>
#include <vector>

namespace detent::local::detail {
namespace p = paged;
namespace {
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid immutable page graph");
}
p::Digest finish(SHA256 &hash) {
	std::array<unsigned char, 32> bytes{};
	hash.getHash(bytes.data());
	p::Digest result;
	std::copy(bytes.begin(), bytes.end(), result.begin());
	return result;
}
} // namespace
struct GraphValidator::Impl {
	enum class Phase { Directory, Zero, Segments, Walk, Finalize, Done };
	enum class NodePhase { Mark, Check, Prefix, Leaf, Children, Finish };
	struct SegmentState {
		std::size_t bitmap = 0;
		std::uint64_t cells = 0, extents = 0;
	};
	struct Namespace {
		std::array<char, 198> name{};
		std::size_t length = 0;
		std::uint64_t revision = 0;
		std::string_view text() const { return {name.data(), length}; }
	};
	struct Frame {
		p::Reference reference;
		p::PageHeader header;
		std::span<const char> bytes;
		std::size_t segment = 0, bit = 0, end = 0, restore = 0, child = 0;
		std::uint64_t keys = 0;
		NodePhase phase = NodePhase::Mark;
	};
	std::shared_ptr<distribution::StorageLedger> account;
	std::shared_ptr<const void> owner;
	std::span<const SegmentInput> segments;
	p::Root expected;
	p::Limits limits;
	PagedSemanticLimits semantic_limits;
	std::uint64_t strings = 0;
	StorageIdentity storage;
	std::vector<SegmentState, distribution::StorageAllocator<SegmentState>> states;
	std::vector<Namespace, distribution::StorageAllocator<Namespace>> namespaces;
	std::array<Frame, 203> frames{};
	std::array<char, 202> key{};
	std::shared_ptr<distribution::OwnedBuffer> bitmap;
	std::optional<PageValidator> page;
	SHA256 segment_hash;
	std::size_t index = 0, offset = 0, bitmap_size = 0, depth = 0, key_size = 0, charge = 0;
	std::uint64_t physical_bytes = 0, extent_bytes = 0, pages = 0, records = 0;
	std::uint32_t bools = 0, ints = 0, maximum_overrides = 0;
	std::uint64_t overrides = 0;
	std::array<std::uint32_t, 257> override_histogram{};
	SegmentRegistry *recorded_registry = nullptr;
	std::size_t recorded_segments = 0, recorded_histogram = 0;
	Phase phase = Phase::Directory;
	bool failed = false, started = false, definitions = false;
	Impl(std::shared_ptr<distribution::StorageLedger> ledger, std::shared_ptr<const void> lifetime,
		 std::span<const SegmentInput> source, p::Root root, StorageIdentity identity, p::Limits bounds,
		 PagedSemanticLimits semantics)
		: account(std::move(ledger)), owner(std::move(lifetime)), segments(source), expected(root),
		  limits(bounds), semantic_limits(semantics), storage(identity),
		  states(distribution::StorageAllocator<SegmentState>(account)),
		  namespaces(distribution::StorageAllocator<Namespace>(account)) {
		require(owner && segments.size() == expected.segments && segments.size() <= limits.segments);
		p::encode_root(expected, limits);
		require(std::any_of(storage.begin(), storage.end(), [](char value) { return value != 0; }));
		states.reserve(segments.size());
		namespaces.reserve(expected.namespaces);
	}
	bool admit(std::size_t amount, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = amount;
		const auto count = std::min(charge, budget.bytes);
		charge -= count;
		budget.bytes -= count;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
	void push(const p::Reference &reference) {
		require(depth < frames.size());
		const auto segment = find_segment(segments, reference.segment);
		require(segment < segments.size());
		const auto bytes = checked_page_bytes(segments[segment], reference);
		require(pages < expected.pages && reference.length <= expected.bytes - extent_bytes);
		auto &frame = frames[depth++];
		frame = {};
		frame.reference = reference;
		frame.segment = segment;
		frame.bytes = bytes;
		frame.restore = key_size;
		frame.bit = reference.offset / 8;
		frame.end = (std::size_t(reference.offset) + reference.length + 7) / 8;
		++pages;
		extent_bytes += reference.length;
		++states[frame.segment].cells;
		states[frame.segment].extents += reference.length;
	}
	void leaf(Frame &frame) {
		const auto parsed = p::decode_key({key.data(), key_size});
		require(parsed.kind == frame.header.kind);
		const auto body = frame.bytes.last(frame.header.body);
		if (parsed.kind == p::PageKind::Namespace) {
			require(!definitions && namespaces.size() < expected.namespaces);
			require(parsed.name_space.size() <= semantic_limits.strings - strings);
			strings += parsed.name_space.size();
			Namespace value;
			value.length = parsed.name_space.size();
			std::copy(parsed.name_space.begin(), parsed.name_space.end(), value.name.begin());
			value.revision = read_integer(body, 0, 8);
			require(value.revision <= expected.cursor.revision &&
					(namespaces.empty() || namespaces.back().text() > value.text()));
			namespaces.push_back(value);
		} else {
			definitions = true;
			require(records < expected.records);
			const auto found = std::lower_bound(
				namespaces.begin(), namespaces.end(), parsed.name_space,
				[](const Namespace &value, std::string_view name) { return value.text() > name; });
			require(found != namespaces.end() && found->text() == parsed.name_space);
			const auto definition = p::validate_definition(body, found->revision);
			require(definition.overrides <= semantic_limits.overrides);
			const auto text = parsed.name_space.size() + parsed.local_name.size() + 1 + body.size() -
							  p::definition_header_bytes -
							  std::uint64_t(definition.overrides) * p::override_header_bytes;
			require(text <= semantic_limits.strings - strings);
			strings += text;
			definition.boolean ? ++bools : ++ints;
			overrides += definition.overrides;
			maximum_overrides = std::max(maximum_overrides, definition.overrides);
			++override_histogram[definition.overrides];
			++records;
		}
		frame.keys = 1;
		frame.phase = NodePhase::Finish;
	}
};
GraphValidator::GraphValidator(std::shared_ptr<distribution::StorageLedger> account,
							   std::shared_ptr<const void> owner, std::span<const SegmentInput> segments,
							   p::Root root, StorageIdentity storage, p::Limits limits,
							   PagedSemanticLimits semantics)
	: impl_(distribution::make_storage_owner<Impl>(account, std::malloc, std::free, account, std::move(owner),
												   segments, root, storage, limits, semantics)) {}
GraphValidator::~GraphValidator() = default;
GraphValidator::GraphValidator(GraphValidator &&) noexcept = default;
void GraphValidator::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed)
		throw std::logic_error("inactive graph validation");
	auto &state = *impl_;
	try {
		while (state.phase != Impl::Phase::Done) {
			if (state.phase == Impl::Phase::Directory) {
				if (state.index == state.segments.size()) {
					state.bitmap = distribution::make_owned_buffer(state.account, state.bitmap_size);
					state.index = 0;
					state.phase = Impl::Phase::Zero;
					continue;
				}
				if (!state.admit(segment_ownership_bytes, budget))
					return;
				const auto &input = state.segments[state.index];
				encode_segment_ownership(input.ownership);
				require(input.ownership.storage == state.storage &&
						input.bytes.size() == input.ownership.length);
				require(!state.index || std::memcmp(state.segments[state.index - 1].ownership.segment.data(),
													input.ownership.segment.data(), 16) < 0);
				require(input.bytes.size() <= state.limits.bytes - state.physical_bytes);
				state.physical_bytes += input.bytes.size();
				state.states.push_back({state.bitmap_size, 0});
				state.bitmap_size += (input.bytes.size() + 63) / 64;
				++state.index;
				continue;
			}
			if (state.phase == Impl::Phase::Zero) {
				const auto count = std::min(state.bitmap_size - state.bitmap->size(), budget.bytes);
				state.bitmap->resize(state.bitmap->size() + count);
				budget.bytes -= count;
				if (state.bitmap->size() != state.bitmap_size)
					return;
				state.phase = Impl::Phase::Segments;
				continue;
			}
			if (state.phase == Impl::Phase::Segments) {
				if (state.index == state.segments.size()) {
					state.phase = Impl::Phase::Walk;
					continue;
				}
				const auto &input = state.segments[state.index];
				const auto count = std::min(input.bytes.size() - state.offset, budget.bytes);
				if (count)
					state.segment_hash.add(input.bytes.data() + state.offset, count);
				state.offset += count;
				budget.bytes -= count;
				if (state.offset != input.bytes.size() || !budget.rows)
					return;
				--budget.rows;
				require(finish(state.segment_hash) == input.ownership.checksum);
				state.segment_hash = SHA256{};
				state.offset = 0;
				++state.index;
				continue;
			}
			if (state.phase == Impl::Phase::Finalize) {
				if (state.index < state.states.size()) {
					if (!budget.rows)
						return;
					--budget.rows;
					require(state.states[state.index++].cells);
					continue;
				}
				require(state.records == state.expected.records &&
						state.namespaces.size() == state.expected.namespaces &&
						state.pages == state.expected.pages && state.extent_bytes == state.expected.bytes);
				state.phase = Impl::Phase::Done;
				continue;
			}
			if (!state.started) {
				if (state.expected.pages) {
					if (!state.admit(p::reference_bytes, budget))
						return;
					state.push(state.expected.tree);
				}
				state.started = true;
			}
			if (!state.depth) {
				state.index = 0;
				state.phase = Impl::Phase::Finalize;
				continue;
			}
			auto &frame = state.frames[state.depth - 1];
			switch (frame.phase) {
			case Impl::NodePhase::Mark:
				while (frame.bit < frame.end) {
					if (!budget.bytes)
						return;
					--budget.bytes;
					auto &value =
						state.bitmap->mutable_bytes()[state.states[frame.segment].bitmap + frame.bit / 8];
					const auto mask = 1u << (frame.bit % 8);
					require(!(static_cast<unsigned char>(value) & mask));
					value = static_cast<char>(static_cast<unsigned char>(value) | mask);
					++frame.bit;
				}
				if (!budget.rows)
					return;
				--budget.rows;
				state.page.emplace(state.account, state.owner, frame.bytes, frame.reference, state.limits);
				frame.phase = Impl::NodePhase::Check;
				break;
			case Impl::NodePhase::Check:
				state.page->step(budget);
				if (!state.page->done())
					return;
				frame.header = state.page->header();
				state.page.reset();
				require(frame.header.kind != p::PageKind::Overflow &&
						frame.header.prefix <= state.key.size() - state.key_size);
				frame.phase = Impl::NodePhase::Prefix;
				state.offset = 0;
				break;
			case Impl::NodePhase::Prefix: {
				const auto count = std::min<std::size_t>(frame.header.prefix - state.offset, budget.bytes);
				std::copy_n(frame.bytes.begin() + p::page_header_bytes + state.offset, count,
							state.key.begin() + state.key_size);
				state.offset += count;
				state.key_size += count;
				budget.bytes -= count;
				if (state.offset != frame.header.prefix)
					return;
				frame.child = frame.header.children;
				frame.phase = frame.header.kind == p::PageKind::Branch ? Impl::NodePhase::Children
																	   : Impl::NodePhase::Leaf;
				break;
			}
			case Impl::NodePhase::Leaf:
				if (!state.admit(frame.header.body + state.key_size, budget))
					return;
				state.leaf(frame);
				break;
			case Impl::NodePhase::Children:
				if (!frame.child) {
					frame.phase = Impl::NodePhase::Finish;
					break;
				}
				if (!state.admit(p::child_bytes + 1, budget))
					return;
				{
					const auto child = p::decode_child(frame.bytes.subspan(
						p::page_header_bytes + frame.header.prefix + (--frame.child) * p::child_bytes,
						p::child_bytes));
					require(state.key_size < state.key.size());
					const auto restore = state.key_size;
					state.key[state.key_size++] = static_cast<char>(child.label);
					state.push(child.reference);
					state.frames[state.depth - 1].restore = restore;
				}
				break;
			case Impl::NodePhase::Finish:
				if (!budget.rows)
					return;
				--budget.rows;
				require(frame.keys == frame.header.keys);
				state.key_size = frame.restore;
				--state.depth;
				if (state.depth) {
					auto &parent = state.frames[state.depth - 1];
					require(frame.keys <= parent.header.keys - parent.keys);
					parent.keys += frame.keys;
				}
				break;
			}
		}
	} catch (...) {
		state.failed = true;
		throw;
	}
}
bool GraphValidator::done() const noexcept {
	return impl_ && !impl_->failed && impl_->phase == Impl::Phase::Done;
}
p::Root GraphValidator::root() const {
	if (!done())
		throw std::logic_error("graph validation incomplete");
	return impl_->expected;
}
PagedStateCounts GraphValidator::counts() const {
	if (!done())
		throw std::logic_error("graph validation incomplete");
	return {impl_->bools, impl_->ints, impl_->overrides, impl_->strings, impl_->maximum_overrides};
}
std::pair<std::shared_ptr<const void>, std::span<const SegmentInput>>
GraphValidator::validated_inputs() const {
	if (!done())
		throw std::logic_error("graph validation incomplete");
	return {impl_->owner, impl_->segments};
}
SegmentReachability GraphValidator::segment_reachability(std::size_t index) const {
	if (!done())
		throw std::logic_error("graph validation incomplete");
	if (index >= impl_->states.size())
		throw std::out_of_range("paged segment reachability index");
	const auto &state = impl_->states[index];
	return {state.cells, state.extents};
}
bool GraphValidator::record_reachability(SegmentRegistry &registry, ValidationBudget &budget) {
	if (!done())
		throw std::logic_error("graph validation incomplete");
	auto &state = *impl_;
	const auto inputs = registry.inputs();
	require(registry.storage_ == state.storage && inputs.data() == state.segments.data() &&
			inputs.size() == state.segments.size());
	if (registry.validated_root_) {
		require(p::encode_root(*registry.validated_root_) == p::encode_root(state.expected));
		return true;
	}
	require(!state.recorded_registry || state.recorded_registry == &registry);
	state.recorded_registry = &registry;
	while (state.recorded_segments < state.states.size()) {
		if (!state.admit(sizeof(SegmentReachability), budget))
			return false;
		registry.reachable_[state.recorded_segments] = segment_reachability(state.recorded_segments);
		++state.recorded_segments;
	}
	while (state.recorded_histogram < state.override_histogram.size()) {
		if (!state.admit(sizeof(std::uint32_t), budget))
			return false;
		registry.override_histogram_[state.recorded_histogram] =
			state.override_histogram[state.recorded_histogram];
		++state.recorded_histogram;
	}
	if (!state.admit(p::root_bytes, budget))
		return false;
	registry.validated_root_ = state.expected;
	return true;
}
ReadView GraphValidator::take(distribution::RetirementDomain &domain) {
	if (!done())
		throw std::logic_error("graph validation incomplete");
	auto &state = *impl_;
	auto root = domain.make<ReadView::Root>(state.owner, PagedReadState{state.expected, state.segments},
											state.bools, state.ints, state.overrides, state.strings);
	impl_.reset();
	return ReadView(std::move(root));
}
bool GraphValidator::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
