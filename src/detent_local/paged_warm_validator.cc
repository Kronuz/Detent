#include "paged_warm_validator.hh"
#include "detent_distribution/rows.hh"
#include "detent_local/paged_definition.hh"
#include "paged_navigation.hh"
#include <sha256.h>
namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
namespace {
void require(bool condition) {
	if (!condition)
		throw std::invalid_argument("invalid warm paged graph");
}
p::Digest finish(SHA256 &hash) {
	std::array<unsigned char, 32> value{};
	hash.getHash(value.data());
	p::Digest result;
	std::copy(value.begin(), value.end(), result.begin());
	return result;
}
} // namespace
struct WarmGraphValidator::Impl {
	struct Namespace {
		std::array<char, 198> text{};
		std::size_t length = 0;
		std::uint64_t original = 0, revision = 0;
		std::string_view name() const { return {text.data(), length}; }
	};
	struct NewState {
		SegmentReachability live;
		std::size_t bitmap = 0;
	};
	struct Frontier {
		p::Reference reference;
		std::array<char, 202> prefix{};
		std::size_t length = 0;
		std::uint64_t keys = 0;
	};
	enum class Phase {
		Seed,
		Namespaces,
		Directory,
		Zero,
		Hashes,
		NewWalk,
		OldWalk,
		Finalize,
		Histogram,
		Done
	};
	enum class NodePhase { Reuse, Mark, Check, Prefix, Leaf, Children, Finish };
	struct Frame {
		p::Reference reference;
		p::PageHeader header;
		std::span<const char> bytes;
		std::size_t segment = 0, restore = 0, child = 0, bit = 0, end = 0;
		std::uint64_t keys = 0;
		NodePhase phase = NodePhase::Reuse;
	};
	std::shared_ptr<d::StorageLedger> account;
	d::RetirementHandle<LeasedPagedRoot> base;
	std::shared_ptr<const void> owner;
	std::span<const SegmentInput> added_segments, base_segments;
	p::Root expected;
	p::Limits limits;
	PagedSemanticLimits semantics;
	PagedStateCounts counts, new_counts{}, removed_counts{};
	std::array<std::int64_t, 257> histogram{};
	std::vector<Namespace, d::StorageAllocator<Namespace>> namespaces;
	std::vector<SegmentReachability, d::StorageAllocator<SegmentReachability>> removed;
	std::vector<NewState, d::StorageAllocator<NewState>> states;
	d::PagedStorage arena;
	d::StableRows<Frontier> frontiers;
	std::optional<PagedCursor> namespace_cursor;
	std::optional<PageValidator> page;
	std::optional<ReuseNavigator> reuse;
	std::shared_ptr<d::OwnedBuffer> bitmap;
	std::array<Frame, 203> frames{};
	std::array<char, 202> key{};
	SHA256 hash;
	std::size_t index = 0, offset = 0, depth = 0, key_size = 0, charge = 0, bitmap_size = 0,
				histogram_index = 0, recorded_index = 0, recorded_histogram = 0;
	std::uint64_t frontier = 0, new_pages = 0, new_bytes = 0, removed_pages = 0, removed_bytes = 0,
				  new_namespaces = 0, removed_namespaces = 0, physical_bytes = 0, live_segments = 0,
				  hashed_bytes = 0, histogram_records = 0, histogram_overrides = 0, definition_checks = 0,
				  old_headers = 0;
	SegmentRegistry *recorded = nullptr;
	Phase phase = Phase::Seed;
	bool failed = false, discarding = false, definitions = false;
	Impl(std::shared_ptr<d::StorageLedger> ledger, d::RetirementHandle<LeasedPagedRoot> actual_base,
		 std::shared_ptr<const void> lifetime, std::span<const SegmentInput> added, p::Root root,
		 p::Limits bounds, PagedSemanticLimits semantic)
		: account(std::move(ledger)), base(std::move(actual_base)), owner(std::move(lifetime)),
		  added_segments(added), expected(root), limits(bounds), semantics(semantic),
		  namespaces(d::StorageAllocator<Namespace>(account)),
		  removed(d::StorageAllocator<SegmentReachability>(account)),
		  states(d::StorageAllocator<NewState>(account)), arena(*account, 8192), frontiers(arena),
		  namespace_cursor(std::in_place, account, base, p::PageKind::Namespace) {
		require(base && owner);
		const auto &prior = base->root();
		base_segments = base->registry()->inputs();
		counts = base->counts();
		require(prior.cursor.same_identity(root.cursor) && root.cursor.revision >= prior.cursor.revision &&
				root.profile == prior.profile && root.namespaces == prior.namespaces &&
				added.size() <= root.segments);
		p::encode_root(root, limits);
		namespaces.reserve(root.namespaces);
		removed.reserve(base_segments.size());
		states.reserve(added.size());
	}
	bool grant(std::size_t bytes, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = bytes;
		auto n = std::min(charge, budget.bytes);
		charge -= n;
		budget.bytes -= n;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
	Namespace &name_space(std::string_view name) {
		auto found = std::lower_bound(namespaces.begin(), namespaces.end(), name,
									  [](const Namespace &row, auto name) { return row.name() < name; });
		require(found != namespaces.end() && found->name() == name);
		return *found;
	}
	void push(p::Reference reference, bool old) {
		require(depth < frames.size());
		auto &frame = frames[depth++];
		frame = {};
		frame.reference = reference;
		frame.restore = key_size;
		const auto prior = find_segment(base_segments, reference.segment);
		if (old || prior != base_segments.size()) {
			require(prior < base_segments.size());
			frame.segment = prior;
			frame.bytes = checked_page_bytes(base_segments[prior], reference);
			if (old)
				frame.header = p::decode_page_header(frame.bytes, limits);
			frame.phase = NodePhase::Reuse;
		} else {
			const auto added = find_segment(added_segments, reference.segment);
			require(added < added_segments.size());
			frame.segment = added;
			frame.bytes = checked_page_bytes(added_segments[added], reference);
			frame.bit = reference.offset / 8;
			frame.end = (std::size_t(reference.offset) + reference.length + 7) / 8;
			frame.phase = NodePhase::Mark;
			require(new_pages < expected.pages && reference.length <= expected.bytes - new_bytes);
			++new_pages;
			new_bytes += reference.length;
			++states[added].live.pages;
			states[added].live.bytes += reference.length;
		}
	}
	void complete() {
		const auto keys = frames[depth - 1].keys;
		key_size = frames[depth - 1].restore;
		--depth;
		if (depth) {
			auto &parent = frames[depth - 1];
			require(keys <= std::uint64_t(expected.records) + expected.namespaces - parent.keys);
			parent.keys += keys;
		} else
			require(keys == std::uint64_t(expected.records) + expected.namespaces);
	}
	void leaf(Frame &frame, bool old) {
		const auto parsed = p::decode_key({key.data(), key_size});
		require(parsed.kind == frame.header.kind);
		const auto body = frame.bytes.last(frame.header.body);
		auto &ns = name_space(parsed.name_space);
		auto &target = old ? removed_counts : new_counts;
		if (parsed.kind == p::PageKind::Namespace) {
			if (old)
				++removed_namespaces;
			else {
				require(!definitions);
				++new_namespaces;
				const auto revision = read_integer(body, 0, 8);
				require(revision >= ns.original && revision <= expected.cursor.revision);
				ns.revision = revision;
			}
			target.strings += parsed.name_space.size();
		} else {
			if (!old)
				definitions = true;
			const auto definition = old ? p::decode_definition_header(body, ns.original)
										: p::validate_definition(body, ns.revision);
			old ? ++old_headers : ++definition_checks;
			if (!old)
				require(definition.overrides <= semantics.overrides);
			definition.boolean ? ++target.bools : ++target.ints;
			target.overrides += definition.overrides;
			target.strings += key_size - 2 + body.size() - p::definition_header_bytes -
							  std::uint64_t(definition.overrides) * p::override_header_bytes;
			histogram[definition.overrides] += old ? -1 : 1;
		}
		frame.keys = 1;
		frame.phase = NodePhase::Finish;
	}
};
WarmGraphValidator::WarmGraphValidator(std::shared_ptr<d::StorageLedger> account,
									   const RecoveredGeneration &base, std::shared_ptr<const void> owner,
									   std::span<const SegmentInput> added, p::Root root, p::Limits limits,
									   PagedSemanticLimits semantics)
	: WarmGraphValidator(std::move(account), base.paged_, std::move(owner), added, root, limits, semantics) {}
WarmGraphValidator::WarmGraphValidator(std::shared_ptr<d::StorageLedger> account,
									   d::RetirementHandle<LeasedPagedRoot> base,
									   std::shared_ptr<const void> owner, std::span<const SegmentInput> added,
									   p::Root root, p::Limits limits, PagedSemanticLimits semantics)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, std::move(base),
										std::move(owner), added, root, limits, semantics)) {}
WarmGraphValidator::~WarmGraphValidator() = default;
WarmGraphValidator::WarmGraphValidator(WarmGraphValidator &&) noexcept = default;
void WarmGraphValidator::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed || impl_->discarding)
		throw std::logic_error("inactive warm graph validation");
	auto &s = *impl_;
	try {
		while (s.phase != Impl::Phase::Done) {
			switch (s.phase) {
			case Impl::Phase::Seed:
				if (s.index < s.base_segments.size()) {
					if (!s.grant(sizeof(SegmentReachability), budget))
						return;
					s.removed.push_back({});
					++s.index;
					break;
				}
				if (s.histogram_index < s.histogram.size()) {
					if (!s.grant(4, budget))
						return;
					s.histogram[s.histogram_index] =
						s.base->registry()->override_histogram()[s.histogram_index];
					++s.histogram_index;
					break;
				}
				s.index = 0;
				s.phase = Impl::Phase::Namespaces;
				break;
			case Impl::Phase::Namespaces:
				s.namespace_cursor->step(budget);
				if (s.namespace_cursor->done()) {
					require(s.namespaces.size() == s.expected.namespaces);
					s.phase = Impl::Phase::Directory;
					break;
				}
				if (!s.namespace_cursor->ready() || !s.grant(256, budget))
					return;
				{
					const auto leaf = s.namespace_cursor->leaf();
					const auto parsed = p::decode_key(leaf.key);
					Impl::Namespace row;
					row.length = parsed.name_space.size();
					require(row.length <= row.text.size());
					std::copy(parsed.name_space.begin(), parsed.name_space.end(), row.text.begin());
					row.original = row.revision = read_integer(leaf.body, 0, 8);
					require(s.namespaces.size() < s.expected.namespaces &&
							(s.namespaces.empty() || s.namespaces.back().name() < row.name()));
					s.namespaces.push_back(row);
					s.namespace_cursor->consume();
				}
				break;
			case Impl::Phase::Directory:
				if (s.index == s.added_segments.size()) {
					if (s.bitmap_size)
						s.bitmap = d::make_owned_buffer(s.account, s.bitmap_size);
					s.index = s.offset = 0;
					s.phase = Impl::Phase::Zero;
					break;
				}
				if (!s.grant(segment_ownership_bytes, budget))
					return;
				{
					const auto &input = s.added_segments[s.index];
					encode_segment_ownership(input.ownership);
					require(
						input.ownership.storage == s.base->storage_identity() &&
						input.ownership.length == input.bytes.size() &&
						(s.index == 0 || std::memcmp(s.added_segments[s.index - 1].ownership.segment.data(),
													 input.ownership.segment.data(), 16) < 0) &&
						find_segment(s.base_segments, input.ownership.segment) == s.base_segments.size());
					require(input.bytes.size() <= s.limits.bytes - s.physical_bytes);
					s.physical_bytes += input.bytes.size();
					s.states.push_back({{}, s.bitmap_size});
					s.bitmap_size += (input.bytes.size() + 63) / 64;
					++s.index;
				}
				break;
			case Impl::Phase::Zero:
				if (s.bitmap) {
					const auto n = std::min(s.bitmap_size - s.bitmap->size(), budget.bytes);
					s.bitmap->resize(s.bitmap->size() + n);
					budget.bytes -= n;
					if (s.bitmap->size() != s.bitmap_size)
						return;
				}
				s.phase = Impl::Phase::Hashes;
				break;
			case Impl::Phase::Hashes:
				if (s.index == s.added_segments.size()) {
					if (s.expected.pages)
						s.push(s.expected.tree, false);
					s.phase = Impl::Phase::NewWalk;
					break;
				}
				{
					const auto &input = s.added_segments[s.index];
					const auto n = std::min(input.bytes.size() - s.offset, budget.bytes);
					if (n)
						s.hash.add(input.bytes.data() + s.offset, n);
					s.offset += n;
					s.hashed_bytes += n;
					budget.bytes -= n;
					if (s.offset != input.bytes.size() || !budget.rows)
						return;
					--budget.rows;
					require(finish(s.hash) == input.ownership.checksum);
					s.hash = SHA256{};
					s.offset = 0;
					++s.index;
				}
				break;
			case Impl::Phase::NewWalk: {
				if (!s.depth) {
					s.key_size = 0;
					if (s.base->root().pages)
						s.push(s.base->root().tree, true);
					s.phase = Impl::Phase::OldWalk;
					break;
				}
				auto &frame = s.frames[s.depth - 1];
				switch (frame.phase) {
				case Impl::NodePhase::Reuse:
					if (!s.reuse)
						s.reuse.emplace(s.account, s.base, frame.reference,
										std::string_view(s.key.data(), s.key_size));
					s.reuse->step(budget);
					if (!s.reuse->done())
						return;
					{
						auto proof = s.reuse->take();
						require(proof && proof->matches(frame.reference, {s.key.data(), s.key_size},
														s.base->descriptor(), s.base->directory_identity(),
														s.base->storage_identity()));
						Impl::Frontier frontier;
						frontier.reference = frame.reference;
						frontier.length = s.key_size;
						frontier.keys = proof->header().keys;
						std::copy_n(s.key.begin(), s.key_size, frontier.prefix.begin());
						s.frontiers.push_back(frontier);
						frame.keys = frontier.keys;
						frame.phase = Impl::NodePhase::Finish;
					}
					break;
				case Impl::NodePhase::Mark:
					while (frame.bit < frame.end) {
						if (!s.grant(8, budget))
							return;
						const auto cell = s.states[frame.segment].bitmap + frame.bit / 8;
						const auto mask = static_cast<unsigned char>(1u << (frame.bit % 8));
						auto &byte = s.bitmap->mutable_bytes()[cell];
						require(!(static_cast<unsigned char>(byte) & mask));
						byte = static_cast<char>(static_cast<unsigned char>(byte) | mask);
						++frame.bit;
					}
					s.page.emplace(s.account, s.owner, frame.bytes, frame.reference, s.limits);
					frame.phase = Impl::NodePhase::Check;
					break;
				case Impl::NodePhase::Check:
					s.page->step(budget);
					if (!s.page->done())
						return;
					frame.header = s.page->header();
					require(frame.header.kind != p::PageKind::Overflow);
					frame.phase = Impl::NodePhase::Prefix;
					break;
				case Impl::NodePhase::Prefix:
					if (s.page && !s.page->discard(budget))
						return;
					s.page.reset();
					if (!s.grant(frame.header.prefix, budget))
						return;
					require(frame.header.prefix <= s.key.size() - s.key_size);
					std::copy_n(frame.bytes.data() + p::page_header_bytes, frame.header.prefix,
								s.key.data() + s.key_size);
					s.key_size += frame.header.prefix;
					frame.phase = frame.header.kind == p::PageKind::Branch ? Impl::NodePhase::Children
																		   : Impl::NodePhase::Leaf;
					break;
				case Impl::NodePhase::Leaf:
					if (!s.grant(frame.header.body + s.key_size, budget))
						return;
					s.leaf(frame, false);
					break;
				case Impl::NodePhase::Children:
					if (frame.child == frame.header.children) {
						require(frame.keys == frame.header.keys);
						frame.phase = Impl::NodePhase::Finish;
						break;
					}
					if (!s.grant(p::child_bytes, budget))
						return;
					{
						s.key_size = frame.restore + frame.header.prefix;
						require(s.key_size < s.key.size());
						const auto child = p::decode_child(frame.bytes.subspan(
							p::page_header_bytes + frame.header.prefix +
								(frame.header.children - 1 - frame.child++) * p::child_bytes,
							p::child_bytes));
						s.key[s.key_size++] = static_cast<char>(child.label);
						s.push(child.reference, false);
					}
					break;
				case Impl::NodePhase::Finish:
					if (s.reuse && !s.reuse->discard(budget))
						return;
					s.reuse.reset();
					if (!s.grant(1, budget))
						return;
					s.complete();
					break;
				}
				break;
			}
			case Impl::Phase::OldWalk: {
				if (!s.depth) {
					require(s.frontier == s.frontiers.size());
					s.index = 0;
					s.phase = Impl::Phase::Finalize;
					break;
				}
				auto &frame = s.frames[s.depth - 1];
				if (frame.phase == Impl::NodePhase::Reuse) {
					if (!s.grant(p::page_header_bytes + frame.header.prefix, budget))
						return;
					if (s.frontier < s.frontiers.size()) {
						const auto &frontier = s.frontiers[s.frontier];
						if (std::string_view(frontier.prefix.data(), frontier.length) ==
							std::string_view(s.key.data(), s.key_size)) {
							require(frame.reference == frontier.reference);
							++s.frontier;
							s.key_size = frame.restore;
							--s.depth;
							break;
						}
					}
					const auto live = s.base->registry()->reachability(frame.segment);
					require(s.removed[frame.segment].pages < live.pages &&
							frame.reference.length <= live.bytes - s.removed[frame.segment].bytes);
					++s.removed[frame.segment].pages;
					s.removed[frame.segment].bytes += frame.reference.length;
					++s.removed_pages;
					s.removed_bytes += frame.reference.length;
					require(frame.header.prefix <= s.key.size() - s.key_size);
					std::copy_n(frame.bytes.data() + p::page_header_bytes, frame.header.prefix,
								s.key.data() + s.key_size);
					s.key_size += frame.header.prefix;
					frame.phase = frame.header.kind == p::PageKind::Branch ? Impl::NodePhase::Children
																		   : Impl::NodePhase::Leaf;
				} else if (frame.phase == Impl::NodePhase::Leaf) {
					if (!s.grant(p::definition_header_bytes + s.key_size, budget))
						return;
					s.leaf(frame, true);
				} else if (frame.phase == Impl::NodePhase::Children) {
					if (frame.child == frame.header.children) {
						frame.phase = Impl::NodePhase::Finish;
						break;
					}
					if (!s.grant(p::child_bytes, budget))
						return;
					s.key_size = frame.restore + frame.header.prefix;
					require(s.key_size < s.key.size());
					const auto child = p::decode_child(
						frame.bytes.subspan(p::page_header_bytes + frame.header.prefix +
												(frame.header.children - 1 - frame.child++) * p::child_bytes,
											p::child_bytes));
					s.key[s.key_size++] = static_cast<char>(child.label);
					s.push(child.reference, true);
				} else {
					if (!s.grant(1, budget))
						return;
					s.key_size = frame.restore;
					--s.depth;
				}
				break;
			}
			case Impl::Phase::Finalize:
				if (s.index < s.base_segments.size()) {
					if (!s.grant(sizeof(SegmentReachability), budget))
						return;
					const auto i = s.index++;
					const auto old = s.base->registry()->reachability(i);
					if (old.pages != s.removed[i].pages) {
						++s.live_segments;
						require(s.base_segments[i].bytes.size() <= s.limits.bytes - s.physical_bytes);
						s.physical_bytes += s.base_segments[i].bytes.size();
					}
					break;
				}
				if (s.index < s.base_segments.size() + s.states.size()) {
					if (!s.grant(sizeof(Impl::NewState), budget))
						return;
					require(s.states[s.index++ - s.base_segments.size()].live.pages);
					++s.live_segments;
					break;
				}
				{
					const auto &prior = s.base->root();
					require(prior.pages - s.removed_pages + s.new_pages == s.expected.pages &&
							prior.bytes - s.removed_bytes + s.new_bytes == s.expected.bytes &&
							s.live_segments == s.expected.segments);
					require(prior.namespaces + s.new_namespaces >= s.removed_namespaces &&
							prior.namespaces + s.new_namespaces - s.removed_namespaces ==
								s.expected.namespaces);
					require(std::uint64_t(s.counts.bools) + s.new_counts.bools >= s.removed_counts.bools &&
							std::uint64_t(s.counts.ints) + s.new_counts.ints >= s.removed_counts.ints &&
							s.counts.overrides + s.new_counts.overrides >= s.removed_counts.overrides &&
							s.counts.strings + s.new_counts.strings >= s.removed_counts.strings);
					const auto bools =
						std::uint64_t(s.counts.bools) + s.new_counts.bools - s.removed_counts.bools;
					const auto ints =
						std::uint64_t(s.counts.ints) + s.new_counts.ints - s.removed_counts.ints;
					require(bools + ints == s.expected.records);
					s.counts.bools = bools;
					s.counts.ints = ints;
					s.counts.overrides += s.new_counts.overrides;
					s.counts.overrides -= s.removed_counts.overrides;
					s.counts.strings += s.new_counts.strings;
					s.counts.strings -= s.removed_counts.strings;
					require(s.counts.strings <= s.semantics.strings);
					s.counts.maximum_overrides = 0;
					s.histogram_index = 0;
					s.phase = Impl::Phase::Histogram;
				}
				break;
			case Impl::Phase::Histogram:
				if (s.histogram_index < s.histogram.size()) {
					if (!s.grant(8, budget))
						return;
					const auto i = s.histogram_index++;
					require(s.histogram[i] >= 0 && std::uint64_t(s.histogram[i]) <= s.expected.records);
					if (s.histogram[i])
						s.counts.maximum_overrides = i;
					s.histogram_records += s.histogram[i];
					s.histogram_overrides += i * s.histogram[i];
					break;
				}
				require(s.histogram_records == s.expected.records &&
						s.histogram_overrides == s.counts.overrides &&
						s.counts.maximum_overrides <= s.semantics.overrides);
				s.phase = Impl::Phase::Done;
				break;
			case Impl::Phase::Done:
				break;
			}
		}
	} catch (...) {
		s.failed = true;
		throw;
	}
}
bool WarmGraphValidator::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->phase == Impl::Phase::Done;
}
p::Root WarmGraphValidator::root() const {
	if (!done())
		throw std::logic_error("unverified warm graph");
	return impl_->expected;
}
PagedStateCounts WarmGraphValidator::counts() const {
	root();
	return impl_->counts;
}
SegmentReachability WarmGraphValidator::removed(std::size_t index) const {
	root();
	return impl_->removed.at(index);
}
SegmentReachability WarmGraphValidator::added(std::size_t index) const {
	root();
	return impl_->states.at(index).live;
}
std::uint64_t WarmGraphValidator::definitions_validated() const noexcept {
	return impl_ ? impl_->definition_checks : 0;
}
std::uint64_t WarmGraphValidator::old_definition_headers() const noexcept {
	return impl_ ? impl_->old_headers : 0;
}
std::uint64_t WarmGraphValidator::hashed_segment_bytes() const {
	root();
	return impl_->hashed_bytes;
}
std::span<const std::int64_t, 257> WarmGraphValidator::override_histogram() const {
	root();
	return impl_->histogram;
}
bool WarmGraphValidator::record_reachability(SegmentRegistry &registry, ValidationBudget &budget) {
	root();
	auto &s = *impl_;
	require(registry.storage_ == s.base->storage_identity());
	if (registry.validated_root_) {
		require(p::encode_root(*registry.validated_root_) == p::encode_root(s.expected));
		return true;
	}
	require(registry.inputs().size() == s.expected.segments && (!s.recorded || s.recorded == &registry));
	s.recorded = &registry;
	const auto inputs = registry.inputs();
	while (s.recorded_index < inputs.size()) {
		if (!s.grant(sizeof(SegmentReachability) + sizeof(SegmentOwnership), budget))
			return false;
		const auto &input = inputs[s.recorded_index];
		const auto old = find_segment(s.base_segments, input.ownership.segment);
		SegmentReachability live;
		const SegmentInput *source;
		if (old != s.base_segments.size()) {
			source = &s.base_segments[old];
			const auto prior = s.base->registry()->reachability(old);
			live = {prior.pages - s.removed[old].pages, prior.bytes - s.removed[old].bytes};
		} else {
			const auto added = find_segment(s.added_segments, input.ownership.segment);
			require(added < s.added_segments.size());
			source = &s.added_segments[added];
			live = s.states[added].live;
		}
		require(live.pages && input.ownership == source->ownership &&
				input.bytes.data() == source->bytes.data() && input.bytes.size() == source->bytes.size());
		registry.reachable_[s.recorded_index++] = live;
	}
	while (s.recorded_histogram < s.histogram.size()) {
		if (!s.grant(4, budget))
			return false;
		registry.override_histogram_[s.recorded_histogram] = s.histogram[s.recorded_histogram];
		++s.recorded_histogram;
	}
	if (!s.grant(p::root_bytes, budget))
		return false;
	registry.validated_root_ = s.expected;
	return true;
}
bool WarmGraphValidator::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	auto &s = *impl_;
	if (!s.discarding) {
		s.discarding = true;
		s.charge = 0;
	}
	if (s.page && !s.page->discard(budget))
		return false;
	s.page.reset();
	if (s.reuse && !s.reuse->discard(budget))
		return false;
	s.reuse.reset();
	if (s.namespace_cursor && !s.namespace_cursor->discard(budget))
		return false;
	s.namespace_cursor.reset();
	while (s.arena.front_capacity()) {
		if (!s.grant(s.arena.front_capacity(), budget))
			return false;
		s.arena.release_one();
	}
	if (s.bitmap) {
		if (!s.grant(s.bitmap->storage_capacity(), budget))
			return false;
		s.bitmap.reset();
	}
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
