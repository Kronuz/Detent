#include "paged_snapshot_builder.hh"
#include "detent_local/paged_definition.hh"
#include "paged_page_builder.hh"
#include "paged_segment_builder.hh"
#include "paged_trie_cursor.hh"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <vector>

namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
namespace {
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid paged snapshot construction");
}
bool nonzero(const StorageIdentity &value) {
	return std::any_of(value.begin(), value.end(), [](char byte) { return byte != 0; });
}
std::size_t key(const void *owner, std::uint64_t ordinal, std::span<char> destination) {
	const auto &source = *static_cast<const d::ValidatedSnapshot *>(owner);
	if (ordinal < source.records().size())
		return p::encode_definition_key(destination, source.records()[ordinal].key);
	return p::encode_namespace_key(destination, source.namespaces()[ordinal - source.records().size()].name);
}
} // namespace
struct PagedImage::Impl {
	p::Root root;
	Descriptor descriptor;
	std::shared_ptr<const d::OwnedBuffer> bytes;
	std::vector<BuiltSegment, d::StorageAllocator<BuiltSegment>> segments;
	bool sealed = false, discarding = false;
	Impl(std::shared_ptr<d::StorageLedger> account, std::size_t capacity)
		: segments(d::StorageAllocator<BuiltSegment>(std::move(account))) {
		segments.reserve(capacity);
	}
};
PagedImage::PagedImage(std::shared_ptr<d::StorageLedger> account, std::size_t capacity)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, capacity)) {}
PagedImage::~PagedImage() = default;
PagedImage::PagedImage(PagedImage &&) noexcept = default;
const p::Root &PagedImage::root() const {
	if (!impl_ || !impl_->sealed || impl_->discarding)
		throw std::logic_error("paged image unavailable");
	return impl_->root;
}
const Descriptor &PagedImage::descriptor() const {
	root();
	return impl_->descriptor;
}
std::span<const BuiltSegment> PagedImage::segments() const {
	root();
	return impl_->segments;
}
std::shared_ptr<const d::OwnedBuffer> PagedImage::bytes() const {
	root();
	return impl_->bytes;
}
bool PagedImage::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	impl_->discarding = true;
	while (!impl_->segments.empty() && budget.rows) {
		--budget.rows;
		impl_->segments.pop_back();
	}
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
PagedImageOwner::PagedImageOwner(PagedImage &&image) : image_(std::move(image)) {
	capacity_remaining_ = image_.bytes()->storage_capacity();
	for (const auto &segment : image_.segments())
		capacity_remaining_ += segment.bytes->storage_capacity();
}
bool PagedImageOwner::retire_step() noexcept {
	// Account backing capacity before releasing buffers in bounded row batches.
	if (capacity_remaining_) {
		capacity_remaining_ -= std::min<std::size_t>(capacity_remaining_, 8192);
		return false;
	}
	ValidationBudget budget{0, 16};
	return image_.discard(budget);
}
struct PagedSnapshotBuilder::Impl {
	enum class Phase {
		Cursor,
		Count,
		Zero,
		Header,
		Overrides,
		Page,
		Pack,
		ReleasePage,
		Commit,
		SealPack,
		RegisterPack,
		ReleasePack,
		Finish,
		Root,
		Done
	};
	std::shared_ptr<const d::ValidatedSnapshot> source;
	std::shared_ptr<d::StorageLedger> account;
	StorageIdentity storage;
	GenerationId generation;
	p::Limits limits;
	PagedImage output;
	std::optional<TrieCursor> cursor;
	std::optional<PageBuilder> page;
	std::optional<SegmentBuilder> segment;
	std::shared_ptr<d::OwnedBuffer> body;
	TrieEvent event{};
	p::Definition definition;
	p::Reference reference;
	std::uint64_t physical_bytes = 0;
	std::size_t body_size = 0, group = 0, offset = 0, charge = 0;
	Phase phase = Phase::Cursor, after_pack = Phase::Page;
	bool failed = false, discarding = false;
	Impl(std::shared_ptr<const d::ValidatedSnapshot> input, StorageIdentity identity, GenerationId id,
		 p::Limits bounds)
		: source(std::move(input)), account(source->storage_account()), storage(identity), generation(id),
		  limits(bounds),
		  output(account,
				 std::min<std::uint64_t>(limits.segments,
										 (source->records().size() + source->namespaces().size()) * 2)) {
		require(nonzero(storage) && nonzero(generation) && source->records().size() <= limits.records &&
				source->namespaces().size() <= limits.namespaces);
		const auto count = source->records().size() + source->namespaces().size();
		require(count <= limits.pages && count <= limits.bytes / p::page_header_bytes &&
				(!count || limits.segments));
		auto &root = output.impl_->root;
		root.cursor = source->cursor();
		root.profile = source->profile();
		root.records = source->records().size();
		root.namespaces = source->namespaces().size();
		cursor.emplace(account, source, key, source->records().size() + source->namespaces().size(), limits);
	}
	bool admit(std::size_t bytes, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = bytes;
		const auto count = std::min(charge, budget.bytes);
		charge -= count;
		budget.bytes -= count;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
	const d::OverrideRow &override_row(std::size_t ordinal) const {
		const auto &wire = std::get<d::WireBoolDef>(source->records()[event.ordinal].definition);
		return source->overrides(wire)[ordinal];
	}
	void leaf() {
		definition = {};
		group = offset = 0;
		if (event.kind == p::PageKind::Namespace) {
			require(event.ordinal >= source->records().size());
			body_size = 8;
		} else {
			require(event.kind == p::PageKind::Definition && event.ordinal < source->records().size());
			const auto &record = source->records()[event.ordinal];
			definition.revision = record.revision;
			body_size = p::definition_header_bytes;
			if (const auto *boolean = std::get_if<d::WireBoolDef>(&record.definition)) {
				require(boolean->overrides.count <= 256);
				definition.boolean = true;
				definition.default_bool = boolean->default_value;
				definition.rollout = boolean->rollout_pct;
				definition.overrides = boolean->overrides.count;
			} else
				definition.default_int = std::get<IntDef>(record.definition).default_value;
		}
		phase = Phase::Count;
	}
	void make_page() {
		page.emplace(account, event.kind, event.keys, event.prefix, event.children,
					 body ? std::span<const char>(body->data(), body->size()) : std::span<const char>{},
					 limits);
		phase = Phase::Page;
	}
};
PagedSnapshotBuilder::PagedSnapshotBuilder(std::shared_ptr<const d::ValidatedSnapshot> source,
										   StorageIdentity storage, GenerationId generation, p::Limits limits)
	: impl_([&] {
		  if (!source)
			  throw std::invalid_argument("missing validated paged snapshot");
		  return d::make_storage_owner<Impl>(source->storage_account(), std::malloc, std::free, source,
											 storage, generation, limits);
	  }()) {}
PagedSnapshotBuilder::~PagedSnapshotBuilder() = default;
PagedSnapshotBuilder::PagedSnapshotBuilder(PagedSnapshotBuilder &&) noexcept = default;
void PagedSnapshotBuilder::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed || impl_->discarding)
		throw std::logic_error("inactive paged snapshot builder");
	auto &state = *impl_;
	auto &image = *state.output.impl_;
	try {
		while (state.phase != Impl::Phase::Done) {
			switch (state.phase) {
			case Impl::Phase::Cursor:
				state.cursor->step(budget);
				if (state.cursor->done()) {
					if (state.segment) {
						state.after_pack = Impl::Phase::Finish;
						state.segment->seal();
						state.phase = Impl::Phase::SealPack;
					} else
						state.phase = Impl::Phase::Finish;
					break;
				}
				if (!state.cursor->ready())
					return;
				state.event = state.cursor->event();
				if (state.event.kind == p::PageKind::Branch)
					state.make_page();
				else
					state.leaf();
				break;
			case Impl::Phase::Count:
				if (state.group < state.definition.overrides) {
					const auto &row = state.override_row(state.group);
					if (!state.admit(row.first.size() + 4, budget))
						return;
					require(row.first.size() <= 128 &&
							state.body_size <= p::page_bytes - p::page_header_bytes -
												   state.event.prefix.size() - 4 - row.first.size());
					state.body_size += row.first.size() + 4;
					++state.group;
					break;
				}
				state.body = d::make_owned_buffer(state.account, state.body_size);
				state.phase = Impl::Phase::Zero;
				break;
			case Impl::Phase::Zero: {
				const auto count = std::min(state.body_size - state.body->size(), budget.bytes);
				state.body->resize(state.body->size() + count);
				budget.bytes -= count;
				if (state.body->size() != state.body_size)
					return;
				state.phase = Impl::Phase::Header;
				break;
			}
			case Impl::Phase::Header:
				if (!state.admit(state.event.kind == p::PageKind::Namespace ? 8 : p::definition_header_bytes,
								 budget))
					return;
				if (state.event.kind == p::PageKind::Namespace) {
					write_integer(
						state.body->mutable_bytes(), 0,
						state.source->namespaces()[state.event.ordinal - state.source->records().size()]
							.revision,
						8);
					state.offset = 8;
				} else {
					p::encode_definition_header(state.body->mutable_bytes().first(p::definition_header_bytes),
												state.definition);
					state.offset = p::definition_header_bytes;
				}
				state.group = 0;
				state.phase = Impl::Phase::Overrides;
				break;
			case Impl::Phase::Overrides:
				if (state.group < state.definition.overrides) {
					const auto &row = state.override_row(state.group);
					const auto size = row.first.size() + 4;
					if (!state.admit(size, budget))
						return;
					const auto *rollout = std::get_if<GroupRollout>(&row.second);
					p::encode_override(state.body->mutable_bytes().subspan(state.offset, size),
									   {row.first, rollout != nullptr,
										rollout ? rollout->rollout_pct
												: static_cast<std::uint8_t>(std::get<bool>(row.second))});
					state.offset += size;
					++state.group;
					break;
				}
				require(state.offset == state.body_size);
				state.body->seal();
				state.make_page();
				break;
			case Impl::Phase::Page:
				state.page->step(budget);
				if (!state.page->done())
					return;
				if (!state.segment)
					state.segment.emplace(state.account, state.storage, state.generation,
										  image.segments.size());
				if (!state.segment->fits(*state.page)) {
					state.after_pack = Impl::Phase::Page;
					state.segment->seal();
					state.phase = Impl::Phase::SealPack;
				} else {
					state.segment->append(*state.page);
					state.phase = Impl::Phase::Pack;
				}
				break;
			case Impl::Phase::Pack:
				state.segment->step(budget);
				if (!state.segment->page_ready())
					return;
				state.reference = state.segment->reference();
				require(image.root.pages < state.limits.pages &&
						state.reference.length <= state.limits.bytes - image.root.bytes);
				++image.root.pages;
				image.root.bytes += state.reference.length;
				state.phase = Impl::Phase::ReleasePage;
				break;
			case Impl::Phase::ReleasePage:
				if (!state.page->discard(budget))
					return;
				state.page.reset();
				state.body.reset();
				state.phase = Impl::Phase::Commit;
				break;
			case Impl::Phase::Commit:
				if (!state.cursor->commit(state.reference, budget))
					return;
				state.phase = Impl::Phase::Cursor;
				break;
			case Impl::Phase::SealPack:
				state.segment->step(budget);
				if (!state.segment->done())
					return;
				state.phase = Impl::Phase::RegisterPack;
				break;
			case Impl::Phase::RegisterPack: {
				if (!state.admit(sizeof(BuiltSegment), budget))
					return;
				const auto ownership = state.segment->ownership();
				require(image.segments.size() < state.limits.segments &&
						ownership.length <= state.limits.bytes - state.physical_bytes);
				state.physical_bytes += ownership.length;
				image.segments.push_back({ownership, state.segment->bytes()});
				state.phase = Impl::Phase::ReleasePack;
				break;
			}
			case Impl::Phase::ReleasePack:
				if (!state.segment->discard(budget))
					return;
				state.segment.reset();
				state.phase = state.after_pack;
				break;
			case Impl::Phase::Finish:
				image.root.tree = state.cursor->root();
				image.root.segments = image.segments.size();
				image.root.logical = image.root.pages ? image.root.tree.logical : p::empty_commitment();
				state.phase = Impl::Phase::Root;
				break;
			case Impl::Phase::Root: {
				if (!state.admit(3 * p::root_bytes, budget))
					return;
				const auto encoded = p::encode_root(image.root, state.limits);
				auto bytes = d::make_owned_buffer(state.account, encoded.size());
				bytes->append({encoded.data(), encoded.size()});
				bytes->seal();
				image.descriptor = {state.generation, p::root_bytes, p::physical_commitment(encoded),
									image.root.cursor, image.root.profile};
				image.bytes = std::move(bytes);
				image.sealed = true;
				state.phase = Impl::Phase::Done;
				break;
			}
			case Impl::Phase::Done:
				break;
			}
		}
	} catch (...) {
		state.failed = true;
		throw;
	}
}
bool PagedSnapshotBuilder::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->phase == Impl::Phase::Done;
}
PagedImage PagedSnapshotBuilder::take() {
	if (!done())
		throw std::logic_error("paged snapshot incomplete");
	auto result = std::move(impl_->output);
	impl_.reset();
	return result;
}
bool PagedSnapshotBuilder::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	impl_->discarding = true;
	if (impl_->page && !impl_->page->discard(budget))
		return false;
	impl_->page.reset();
	if (impl_->cursor && !impl_->cursor->discard(budget))
		return false;
	impl_->cursor.reset();
	if (impl_->segment && !impl_->segment->discard(budget))
		return false;
	impl_->segment.reset();
	if (!impl_->output.discard(budget))
		return false;
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
