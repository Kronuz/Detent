#include "paged_segment_builder.hh"
#include <algorithm>
#include <sha256.h>
#include <stdexcept>

namespace detent::local::detail {
namespace p = paged;
namespace {
void require(bool ok) {
	if (!ok)
		throw std::invalid_argument("invalid paged segment builder");
}
p::Digest finish(SHA256 &hash) {
	std::array<unsigned char, 32> bytes{};
	hash.getHash(bytes.data());
	p::Digest result;
	std::copy(bytes.begin(), bytes.end(), result.begin());
	return result;
}
bool nonzero(const StorageIdentity &identity) {
	return std::any_of(identity.begin(), identity.end(), [](char value) { return value != 0; });
}
} // namespace
struct SegmentBuilder::Impl {
	enum class Phase { Idle, Padding, Page, Seal, Done };
	SegmentOwnership ownership;
	std::shared_ptr<distribution::OwnedBuffer> output;
	std::shared_ptr<const distribution::OwnedBuffer> page;
	p::Reference reference;
	SHA256 hash;
	std::size_t offset = 0;
	std::uint64_t pages = 0;
	Phase phase = Phase::Idle;
	Impl(std::shared_ptr<distribution::StorageLedger> account, StorageIdentity storage,
		 GenerationId publication, std::uint64_t ordinal) {
		require(nonzero(storage) && nonzero(publication));
		ownership.storage = storage;
		SHA256 identity;
		constexpr std::string_view domain = "detent.local.segment.v2.identity";
		identity.add(domain.data(), domain.size());
		identity.add(storage.data(), storage.size());
		identity.add(publication.data(), publication.size());
		std::array<char, 8> ordinal_bytes{};
		write_integer(ordinal_bytes, 0, ordinal, 8);
		identity.add(ordinal_bytes.data(), ordinal_bytes.size());
		const auto digest = finish(identity);
		std::copy_n(digest.begin(), ownership.segment.size(), ownership.segment.begin());
		require(nonzero(ownership.segment));
		output = distribution::make_owned_buffer(std::move(account), p::segment_bytes);
	}
};
SegmentBuilder::SegmentBuilder(std::shared_ptr<distribution::StorageLedger> account, StorageIdentity storage,
							   GenerationId publication, std::uint64_t ordinal)
	: impl_(distribution::make_storage_owner<Impl>(account, std::malloc, std::free, account, storage,
												   publication, ordinal)) {}
SegmentBuilder::~SegmentBuilder() = default;
SegmentBuilder::SegmentBuilder(SegmentBuilder &&) noexcept = default;
bool SegmentBuilder::fits(const PageBuilder &page) const {
	if (!impl_ || impl_->phase != Impl::Phase::Idle)
		throw std::logic_error("segment is not available for append");
	const auto start = (impl_->output->size() + 7) & ~std::size_t(7);
	return start <= p::segment_bytes && page.bytes()->size() <= p::segment_bytes - start;
}
void SegmentBuilder::append(const PageBuilder &page) {
	require(fits(page));
	auto &state = *impl_;
	state.page = page.bytes();
	const auto start = (state.output->size() + 7) & ~std::size_t(7);
	state.reference = {state.ownership.segment, static_cast<std::uint32_t>(start),
					   static_cast<std::uint32_t>(state.page->size()), page.physical(), page.logical()};
	state.offset = 0;
	state.phase = Impl::Phase::Padding;
}
void SegmentBuilder::step(ValidationBudget &budget) {
	if (!impl_)
		throw std::logic_error("inactive segment builder");
	auto &state = *impl_;
	if (state.phase == Impl::Phase::Padding) {
		const auto size = state.output->size();
		const auto count = std::min<std::size_t>(state.reference.offset - size, budget.bytes);
		state.output->resize(size + count);
		state.hash.add(state.output->data() + size, count);
		budget.bytes -= count;
		if (state.output->size() != state.reference.offset)
			return;
		state.phase = Impl::Phase::Page;
	}
	if (state.phase == Impl::Phase::Page) {
		const auto count = std::min(state.page->size() - state.offset, budget.bytes);
		const auto bytes = state.page->view().substr(state.offset, count);
		state.output->append(bytes);
		state.hash.add(bytes.data(), bytes.size());
		state.offset += count;
		budget.bytes -= count;
		if (state.offset != state.page->size() || !budget.rows)
			return;
		--budget.rows;
		state.page.reset();
		++state.pages;
		state.phase = Impl::Phase::Idle;
	}
	if (state.phase == Impl::Phase::Seal && budget.rows) {
		--budget.rows;
		state.ownership.length = state.output->size();
		state.ownership.checksum = finish(state.hash);
		state.output->seal_prefix();
		state.phase = Impl::Phase::Done;
	}
}
bool SegmentBuilder::page_ready() const noexcept {
	return impl_ && impl_->pages && impl_->phase == Impl::Phase::Idle;
}
p::Reference SegmentBuilder::reference() const {
	if (!page_ready())
		throw std::logic_error("segment page is not complete");
	return impl_->reference;
}
void SegmentBuilder::seal() {
	if (!page_ready())
		throw std::logic_error("cannot seal an empty or incomplete segment");
	impl_->phase = Impl::Phase::Seal;
}
bool SegmentBuilder::done() const noexcept { return impl_ && impl_->phase == Impl::Phase::Done; }
std::shared_ptr<const distribution::OwnedBuffer> SegmentBuilder::bytes() const {
	if (!done())
		throw std::logic_error("segment is not sealed");
	return impl_->output;
}
SegmentOwnership SegmentBuilder::ownership() const {
	if (!done())
		throw std::logic_error("segment is not sealed");
	return impl_->ownership;
}
bool SegmentBuilder::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
