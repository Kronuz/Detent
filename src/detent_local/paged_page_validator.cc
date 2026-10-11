#include "paged_page_validator.hh"
#include <algorithm>
#include <sha256.h>
#include <stdexcept>

namespace detent::local::detail {
namespace p = paged;
namespace {
constexpr std::string_view domain = "detent.local.logical.v2.node";
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid immutable page");
}
p::Digest finish(SHA256 &hash) {
	std::array<unsigned char, 32> bytes{};
	hash.getHash(bytes.data());
	p::Digest result;
	std::copy(bytes.begin(), bytes.end(), result.begin());
	return result;
}
} // namespace
struct PageValidator::Impl {
	enum class Phase { Physical, Frame, Prefix, Children, Body, Seal, Done };
	std::shared_ptr<const void> owner;
	std::span<const char> bytes;
	p::Reference reference;
	p::PageHeader header;
	std::array<char, domain.size() + 16> frame{};
	SHA256 physical, logical;
	std::size_t offset = 0, child = 0, charge = 0;
	int previous = -1;
	Phase phase = Phase::Physical;
	bool failed = false;
	Impl(std::shared_ptr<const void> lifetime, std::span<const char> source, p::Reference expected,
		 p::Limits limits)
		: owner(std::move(lifetime)), bytes(source), reference(expected) {
		require(owner && bytes.size() == reference.length);
		p::encode_reference(reference);
		header = p::decode_page_header(bytes, limits);
		require(header.logical == reference.logical);
		std::copy(domain.begin(), domain.end(), frame.begin());
		write_integer(frame, domain.size(), static_cast<std::uint32_t>(header.kind), 4);
		write_integer(frame, domain.size() + 4, header.prefix, 4);
		write_integer(frame, domain.size() + 8, header.children, 4);
		write_integer(frame, domain.size() + 12, header.body, 4);
	}
	void next(Phase value) {
		phase = value;
		offset = 0;
	}
	bool hash(SHA256 &digest, std::span<const char> source, ValidationBudget &budget) {
		const auto count = std::min(source.size() - offset, budget.bytes);
		if (count)
			digest.add(source.data() + offset, count);
		offset += count;
		budget.bytes -= count;
		return offset == source.size();
	}
};
PageValidator::PageValidator(std::shared_ptr<distribution::StorageLedger> account,
							 std::shared_ptr<const void> owner, std::span<const char> bytes,
							 p::Reference reference, p::Limits limits)
	: impl_(distribution::make_storage_owner<Impl>(account, std::malloc, std::free, std::move(owner), bytes,
												   reference, limits)) {}
PageValidator::~PageValidator() = default;
PageValidator::PageValidator(PageValidator &&) noexcept = default;
void PageValidator::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed)
		throw std::logic_error("inactive page validation");
	auto &state = *impl_;
	try {
		while (state.phase != Impl::Phase::Done) {
			switch (state.phase) {
			case Impl::Phase::Physical:
				if (!state.hash(state.physical, state.bytes, budget) || !budget.rows)
					return;
				--budget.rows;
				require(finish(state.physical) == state.reference.physical);
				state.next(Impl::Phase::Frame);
				break;
			case Impl::Phase::Frame:
				if (!state.hash(state.logical, state.frame, budget))
					return;
				state.next(Impl::Phase::Prefix);
				break;
			case Impl::Phase::Prefix:
				if (!state.hash(state.logical, state.bytes.subspan(p::page_header_bytes, state.header.prefix),
								budget))
					return;
				state.next(Impl::Phase::Children);
				break;
			case Impl::Phase::Children:
				if (state.child == state.header.children) {
					state.next(Impl::Phase::Body);
					break;
				}
				if (!budget.rows)
					return;
				if (!state.charge)
					state.charge = p::child_bytes + 33;
				{
					const auto count = std::min(state.charge, budget.bytes);
					state.charge -= count;
					budget.bytes -= count;
					if (state.charge)
						return;
				}
				{
					const auto child = p::decode_child(state.bytes.subspan(
						p::page_header_bytes + state.header.prefix + state.child * p::child_bytes,
						p::child_bytes));
					require(child.label > state.previous);
					state.previous = child.label;
					state.logical.add(&child.label, 1);
					state.logical.add(child.reference.logical.data(), child.reference.logical.size());
					++state.child;
					--budget.rows;
				}
				break;
			case Impl::Phase::Body:
				if (!state.hash(state.logical, state.bytes.last(state.header.body), budget))
					return;
				state.next(Impl::Phase::Seal);
				break;
			case Impl::Phase::Seal:
				if (!budget.rows)
					return;
				--budget.rows;
				require(finish(state.logical) == state.reference.logical);
				state.next(Impl::Phase::Done);
				break;
			case Impl::Phase::Done:
				break;
			}
		}
	} catch (...) {
		state.failed = true;
		throw;
	}
}
bool PageValidator::done() const noexcept {
	return impl_ && !impl_->failed && impl_->phase == Impl::Phase::Done;
}
p::PageHeader PageValidator::header() const {
	if (!done())
		throw std::logic_error("page validation incomplete");
	return impl_->header;
}
bool PageValidator::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
