#include "paged_dependency_walk.hh"
#include "paged_navigation.hh"
#include <algorithm>
#include <stdexcept>
namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
namespace {
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid paged dependency walk");
}
} // namespace
struct PagedDependencyWalk::Impl {
	struct Frame {
		p::Reference reference;
		std::span<const char> bytes;
		p::PageHeader header;
		std::size_t context = 0, children = 0;
		bool checked = false;
	};
	std::shared_ptr<d::StorageLedger> account;
	std::shared_ptr<const void> source;
	Lookup lookup;
	p::Root root;
	StorageIdentity storage;
	p::Limits limits;
	std::array<Frame, 203> frames{};
	std::optional<PageValidator> page;
	std::size_t depth = 0, charge = 0;
	std::uint64_t pages = 0, extents = 0;
	bool waiting = false, complete = false, failed = false, discarding = false;
	Impl(std::shared_ptr<d::StorageLedger> account, std::shared_ptr<const void> source, Lookup lookup,
		 p::Root root, StorageIdentity storage, p::Limits limits)
		: account(std::move(account)), source(std::move(source)), lookup(lookup), root(root),
		  storage(storage), limits(limits) {
		require(this->source && lookup);
		p::encode_root(root, limits);
		if (root.pages)
			push(root.tree, 0);
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
	void push(p::Reference reference, std::size_t context) {
		p::encode_reference(reference);
		require(depth < frames.size() && pages < root.pages && reference.length <= root.bytes - extents);
		frames[depth++] = {reference, {}, {}, context};
		++pages;
		extents += reference.length;
	}
};
PagedDependencyWalk::PagedDependencyWalk(std::shared_ptr<d::StorageLedger> account,
										 std::shared_ptr<const void> source, Lookup lookup, p::Root root,
										 StorageIdentity storage, p::Limits limits)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, std::move(source), lookup,
										root, storage, limits)) {}
PagedDependencyWalk::~PagedDependencyWalk() = default;
PagedDependencyWalk::PagedDependencyWalk(PagedDependencyWalk &&) noexcept = default;
void PagedDependencyWalk::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed || impl_->discarding)
		throw std::logic_error("dependency walk unavailable");
	auto &s = *impl_;
	try {
		while (!s.complete) {
			if (!s.depth) {
				if (!s.admit(16, budget))
					return;
				require(s.pages == s.root.pages && s.extents == s.root.bytes);
				s.complete = true;
				return;
			}
			auto &frame = s.frames[s.depth - 1];
			if (!frame.checked) {
				if (!s.page) {
					if (!s.waiting && !s.admit(p::reference_bytes, budget))
						return;
					if (!budget.rows && s.waiting)
						return;
					auto input = s.lookup(s.source.get(), frame.reference.segment);
					if (!input) {
						s.waiting = true;
						return;
					}
					require(input->ownership.storage == s.storage &&
							input->ownership.segment == frame.reference.segment &&
							input->ownership.length == input->bytes.size() &&
							input->bytes.size() <= p::segment_bytes);
					frame.bytes = checked_page_bytes(*input, frame.reference);
					s.page.emplace(s.account, s.source, frame.bytes, frame.reference, s.limits);
					s.waiting = false;
				}
				s.page->step(budget);
				if (!s.page->done())
					return;
				frame.header = s.page->header();
				if (!s.page->discard(budget))
					return;
				s.page.reset();
				require(frame.header.prefix <= 202 - frame.context);
				frame.context += frame.header.prefix;
				frame.children = frame.header.children;
				frame.checked = true;
			}
			if (frame.children) {
				if (!s.admit(p::child_bytes + 1, budget))
					return;
				require(frame.context < 202);
				auto child = p::decode_child(frame.bytes.subspan(p::page_header_bytes + frame.header.prefix +
																	 (--frame.children) * p::child_bytes,
																 p::child_bytes));
				s.push(child.reference, frame.context + 1);
			} else {
				if (!budget.rows)
					return;
				--budget.rows;
				--s.depth;
			}
		}
	} catch (...) {
		s.failed = true;
		throw;
	}
}
std::optional<StorageIdentity> PagedDependencyWalk::missing() const {
	if (!impl_ || impl_->failed || impl_->discarding)
		throw std::logic_error("dependency walk unavailable");
	if (!impl_->waiting)
		return {};
	return impl_->frames[impl_->depth - 1].reference.segment;
}
bool PagedDependencyWalk::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->complete;
}
bool PagedDependencyWalk::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	impl_->discarding = true;
	if (impl_->page && !impl_->page->discard(budget))
		return false;
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
