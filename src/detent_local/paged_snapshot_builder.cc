#include "paged_snapshot_builder.hh"
#include "borrowed_snapshot_source.hh"
#include "detent_local/paged_definition.hh"
#include "paged_image_state.hh"
#include "paged_leaf_body.hh"
#include "paged_packer.hh"
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
bool nonzero(const GenerationId &value) {
	return std::any_of(value.begin(), value.end(), [](char byte) { return byte != 0; });
}
std::size_t key(const void *owner, std::uint64_t ordinal, std::span<char> destination) {
	const auto &source = *static_cast<const d::ValidatedSnapshot *>(owner);
	if (ordinal < source.records().size())
		return p::encode_definition_key(destination, source.records()[ordinal].key);
	return p::encode_namespace_key(destination,
								   source.namespaces()[ordinal - source.records().size()].name);
}
p::Override snapshot_override(const void *owner, std::uint64_t ordinal, std::uint32_t index) {
	const auto &source = *static_cast<const d::ValidatedSnapshot *>(owner);
	const auto &row = source.overrides(source.records()[ordinal].definition)[index];
	return {row.first, row.second};
}

std::size_t borrowed_key(const void *owner, std::uint64_t ordinal, std::span<char> destination) {
	return static_cast<const BorrowedSnapshotSource *>(owner)->key(ordinal, destination);
}
bool borrowed_ready(const void *owner, std::uint64_t ordinal, ValidationBudget &budget) {
	return static_cast<const BorrowedSnapshotSource *>(owner)->prepare(ordinal, budget);
}
p::Override borrowed_override(const void *owner, std::uint64_t ordinal, std::uint32_t index) {
	return static_cast<const BorrowedSnapshotSource *>(owner)->override_value(ordinal, index);
}

} // namespace
struct PagedSnapshotBuilder::Impl {
	enum class Phase {
		Cursor,
		Body,
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
	std::shared_ptr<const BorrowedSnapshotSource> borrowed;
	std::shared_ptr<d::StorageLedger> account;
	GenerationId generation;
	std::uint32_t ordinal = 0;
	p::Limits limits;
	PagedImage output;
	std::optional<TrieCursor> cursor;
	PagedPacker packer;
	std::optional<SegmentBuilder> segment;
	std::shared_ptr<const d::OwnedBuffer> body;
	std::optional<LeafBody> leaf_body;
	TrieEvent event{};
	p::Reference reference;
	std::uint64_t physical_bytes = 0;
	std::size_t charge = 0;
	Phase phase = Phase::Cursor, after_pack = Phase::Page;
	bool finishing = false;
	bool failed = false, discarding = false;
	Impl(std::shared_ptr<const d::ValidatedSnapshot> input, DerivedGeneration name, p::Limits bounds)
		: source(std::move(input)), account(source->storage_account()), generation(name.id), ordinal(name.ordinal), limits(bounds), output(account, 0), packer(account, limits) {
		require(nonzero(generation) && source->records().size() <= limits.records &&
				source->namespaces().size() <= limits.namespaces);
		const auto count = source->records().size() + source->namespaces().size();
		require(!count ||
				(limits.pages && limits.segments && limits.bytes >= p::minimum_page_bytes));
		auto &root = output.impl_->root;
		root.cursor = source->cursor();
		root.profile = source->profile();
		root.records = source->records().size();
		root.namespaces = source->namespaces().size();
		cursor.emplace(account, source, key, source->records().size() + source->namespaces().size(),
					   limits);
	}
	Impl(std::shared_ptr<const BorrowedSnapshotSource> input, DerivedGeneration name, p::Limits bounds)
		: borrowed(std::move(input)), account(borrowed->storage_account()), generation(name.id), ordinal(name.ordinal), limits(bounds), output(account, 0), packer(account, limits) {
		require(nonzero(generation) && borrowed->records() <= limits.records &&
				borrowed->namespaces() <= limits.namespaces);
		const auto count = std::uint64_t(borrowed->records()) + borrowed->namespaces();
		require(!count ||
				(limits.pages && limits.segments && limits.bytes >= p::minimum_page_bytes));
		auto &root = output.impl_->root;
		root.cursor = borrowed->descriptor().cursor;
		root.profile = borrowed->descriptor().profile;
		root.records = borrowed->records();
		root.namespaces = borrowed->namespaces();
		cursor.emplace(account, borrowed, borrowed_key, count, limits, borrowed_ready);
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
	void leaf() {
		if (borrowed) {
			if (event.kind == p::PageKind::Namespace)
				leaf_body.emplace(account, borrowed->watermark(event.ordinal));
			else {
				require(event.kind == p::PageKind::Definition);
				leaf_body.emplace(account, borrowed->definition(event.ordinal), borrowed.get(),
								  event.ordinal, borrowed_override,
								  p::maximum_leaf_body_bytes - event.prefix.size());
			}
			phase = Phase::Body;
			return;
		}
		if (event.kind == p::PageKind::Namespace) {
			require(event.ordinal >= source->records().size());
			leaf_body.emplace(
				account, source->namespaces()[event.ordinal - source->records().size()].revision);
		} else {
			require(event.kind == p::PageKind::Definition &&
					event.ordinal < source->records().size());
			const auto &record = source->records()[event.ordinal];
			const auto &wire = record.definition;
			require(wire.overrides.count <= 256);
			const auto definition = p::Definition{record.revision, wire.default_value, wire.rollout_pct,
												  wire.rollout_value, static_cast<std::uint32_t>(wire.overrides.count)};
			leaf_body.emplace(account, definition, source.get(), event.ordinal, snapshot_override,
							  p::maximum_leaf_body_bytes - event.prefix.size());
		}
		phase = Phase::Body;
	}

	void make_page() {
		packer.push(event, body ? std::span<const char>(body->data(), body->size())
								: std::span<const char>{});
		phase = Phase::Page;
	}
};
PagedSnapshotBuilder::PagedSnapshotBuilder(std::shared_ptr<const d::ValidatedSnapshot> source,
										   DerivedGeneration generation, p::Limits limits)
	: impl_([&] {
		  if (!source)
			  throw std::invalid_argument("missing validated paged snapshot");
		  return d::make_storage_owner<Impl>(source->storage_account(), std::malloc, std::free,
											 source, generation, limits);
	  }()) {}
PagedSnapshotBuilder::PagedSnapshotBuilder(RecoveredGeneration generation,
										   std::shared_ptr<d::StorageLedger> account,
										   DerivedGeneration id, p::Limits limits)
	: impl_([&] {
		  if (!account)
			  throw std::invalid_argument("borrowed snapshot account missing");
		  auto source = std::allocate_shared<BorrowedSnapshotSource>(
			  d::StorageAllocator<BorrowedSnapshotSource>(account), std::move(generation), account);
		  return d::make_storage_owner<Impl>(
			  account, std::malloc, std::free,
			  std::shared_ptr<const BorrowedSnapshotSource>(std::move(source)), id,
			  limits);
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
					state.packer.finish();
					state.finishing = true;
					state.phase = Impl::Phase::Page;
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
			case Impl::Phase::Body:
				state.leaf_body->step(budget);
				if (!state.leaf_body->done())
					return;
				state.body = state.leaf_body->bytes();
				state.make_page();
				break;

			case Impl::Phase::Page:
				state.packer.step(budget);
				if (state.packer.done()) {
					if (state.segment) {
						state.after_pack = Impl::Phase::Finish;
						state.segment->seal();
						state.phase = Impl::Phase::SealPack;
					} else
						state.phase = Impl::Phase::Finish;
				} else if (state.packer.ready()) {
					if (!state.segment)
						state.segment.emplace(state.account, state.generation,
											  image.segments.size());
					const auto page = state.packer.page();
					if (!state.segment->fits(page)) {
						state.after_pack = Impl::Phase::Page;
						state.segment->seal();
						state.phase = Impl::Phase::SealPack;
					} else {
						state.segment->append(page);
						state.phase = Impl::Phase::Pack;
					}
				} else if (state.packer.accepted()) {
					require(!state.finishing);
					state.phase = Impl::Phase::ReleasePage;
				} else
					return;
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
				state.phase = Impl::Phase::Commit;
				break;
			case Impl::Phase::Commit:
				if (!state.packer.commit(state.reference, budget))
					return;
				state.phase = Impl::Phase::Page;
				break;
			case Impl::Phase::ReleasePage:
				state.body.reset();
				if (state.leaf_body && !state.leaf_body->discard(budget))
					return;
				state.leaf_body.reset();
				if (!state.cursor->commit(budget))
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
				image.root.tree = state.packer.root();
				image.root.segments = image.segments.size();
				state.phase = Impl::Phase::Root;
				break;
			case Impl::Phase::Root: {
				if (!state.admit(3 * p::root_bytes, budget))
					return;
				const auto encoded = p::encode_root(image.root, state.limits);
				auto bytes = d::make_owned_buffer(state.account, encoded.size());
				bytes->append({encoded.data(), encoded.size()});
				bytes->seal();
				image.descriptor = {state.generation, p::root_bytes, p::metadata_commitment(encoded),
									image.root.cursor, image.root.profile, state.ordinal};
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
	if (!impl_->packer.discard(budget))
		return false;
	if (impl_->cursor && !impl_->cursor->discard(budget))
		return false;
	impl_->cursor.reset();
	if (impl_->segment && !impl_->segment->discard(budget))
		return false;
	impl_->segment.reset();
	impl_->body.reset();
	if (impl_->leaf_body && !impl_->leaf_body->discard(budget))
		return false;
	impl_->leaf_body.reset();
	if (!impl_->output.discard(budget))
		return false;
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
