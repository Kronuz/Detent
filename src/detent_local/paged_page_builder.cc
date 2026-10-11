#include "paged_page_builder.hh"
#include <algorithm>
#include <sha256.h>
#include <stdexcept>

namespace detent::local::detail {
namespace p = paged;
namespace {
constexpr std::string_view domain = "detent.local.logical.v2.node";
void require(bool ok) {
	if (!ok)
		throw std::invalid_argument("invalid paged builder input");
}
p::Digest finish(SHA256 &hash) {
	std::array<unsigned char, 32> value{};
	hash.getHash(value.data());
	p::Digest result;
	std::copy(value.begin(), value.end(), result.begin());
	return result;
}
} // namespace
struct PageBuilder::Impl {
	enum class Phase { Zero, Frame, Prefix, Children, Body, Header, Physical, Seal, Done };
	p::PageHeader header;
	std::string_view prefix;
	std::span<const p::Child> children;
	std::span<const char> body;
	std::array<char, domain.size() + 16> frame{};
	std::shared_ptr<distribution::OwnedBuffer> output;
	SHA256 logical_hash, physical_hash;
	p::Digest physical{};
	Phase phase = Phase::Zero;
	std::size_t offset = 0, child = 0, charge = 0;
	int previous = -1;
	bool failed = false;
	Impl(std::shared_ptr<distribution::StorageLedger> account, p::PageKind kind, std::uint64_t keys,
		 std::string_view key_prefix, std::span<const p::Child> entries, std::span<const char> value)
		: prefix(key_prefix), children(entries), body(value) {
		require(prefix.size() <= 512 && children.size() <= 256 && body.size() <= p::page_bytes &&
				kind != p::PageKind::Overflow);
		const auto length =
			p::page_header_bytes + prefix.size() + p::child_bytes * children.size() + body.size();
		require(length <= p::page_bytes);
		header = {kind,
				  static_cast<std::uint32_t>(length),
				  static_cast<std::uint32_t>(prefix.size()),
				  static_cast<std::uint32_t>(children.size()),
				  static_cast<std::uint32_t>(body.size()),
				  keys,
				  {}};
		header.logical.fill(1);
		p::encode_page_header(header);
		std::copy(domain.begin(), domain.end(), frame.begin());
		write_integer(frame, domain.size(), static_cast<std::uint32_t>(kind), 4);
		write_integer(frame, domain.size() + 4, prefix.size(), 4);
		write_integer(frame, domain.size() + 8, children.size(), 4);
		write_integer(frame, domain.size() + 12, body.size(), 4);
		output = distribution::make_owned_buffer(std::move(account), length);
	}
	void next(Phase value) {
		phase = value;
		offset = 0;
	}
	bool copy(std::span<const char> source, std::size_t destination, ValidationBudget &budget) {
		const auto count = std::min(source.size() - offset, budget.bytes);
		if (count) {
			std::copy_n(source.begin() + offset, count,
						output->mutable_bytes().begin() + destination + offset);
			logical_hash.add(source.data() + offset, count);
			offset += count;
			budget.bytes -= count;
		}
		return offset == source.size();
	}
};
PageBuilder::PageBuilder(std::shared_ptr<distribution::StorageLedger> account, p::PageKind kind,
						 std::uint64_t keys, std::string_view prefix, std::span<const p::Child> children,
						 std::span<const char> body)
	: impl_(distribution::make_storage_owner<Impl>(account, std::malloc, std::free, account, kind, keys,
												   prefix, children, body)) {}
PageBuilder::~PageBuilder() = default;
PageBuilder::PageBuilder(PageBuilder &&) noexcept = default;
void PageBuilder::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed)
		throw std::logic_error("inactive paged page builder");
	auto &state = *impl_;
	try {
		while (state.phase != Impl::Phase::Done) {
			switch (state.phase) {
			case Impl::Phase::Zero: {
				const auto count =
					std::min<std::size_t>(state.header.length - state.output->size(), budget.bytes);
				state.output->resize(state.output->size() + count);
				budget.bytes -= count;
				if (state.output->size() != state.header.length)
					return;
				state.next(Impl::Phase::Frame);
				break;
			}
			case Impl::Phase::Frame: {
				const auto count = std::min(state.frame.size() - state.offset, budget.bytes);
				state.logical_hash.add(state.frame.data() + state.offset, count);
				state.offset += count;
				budget.bytes -= count;
				if (state.offset != state.frame.size())
					return;
				state.next(Impl::Phase::Prefix);
				break;
			}
			case Impl::Phase::Prefix:
				if (!state.copy({state.prefix.data(), state.prefix.size()}, p::page_header_bytes, budget))
					return;
				state.next(Impl::Phase::Children);
				break;
			case Impl::Phase::Children:
				if (state.child == state.children.size()) {
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
					const auto &child = state.children[state.child];
					require(child.label > state.previous);
					const auto encoded = p::encode_child(child);
					std::copy(encoded.begin(), encoded.end(),
							  state.output->mutable_bytes().begin() + p::page_header_bytes +
								  state.prefix.size() + state.child * p::child_bytes);
					state.logical_hash.add(&child.label, 1);
					state.logical_hash.add(child.reference.logical.data(), 32);
					state.previous = child.label;
					++state.child;
					--budget.rows;
				}
				break;
			case Impl::Phase::Body:
				if (!state.copy(state.body,
								p::page_header_bytes + state.prefix.size() +
									state.children.size() * p::child_bytes,
								budget))
					return;
				if (!budget.rows)
					return;
				--budget.rows;
				state.header.logical = finish(state.logical_hash);
				state.charge = p::page_header_bytes;
				state.next(Impl::Phase::Header);
				break;
			case Impl::Phase::Header: {
				const auto count = std::min(state.charge, budget.bytes);
				state.charge -= count;
				budget.bytes -= count;
				if (state.charge)
					return;
				const auto encoded = p::encode_page_header(state.header);
				std::copy(encoded.begin(), encoded.end(), state.output->mutable_bytes().begin());
				state.next(Impl::Phase::Physical);
				break;
			}
			case Impl::Phase::Physical: {
				const auto count = std::min(state.output->size() - state.offset, budget.bytes);
				state.physical_hash.add(state.output->data() + state.offset, count);
				state.offset += count;
				budget.bytes -= count;
				if (state.offset != state.output->size())
					return;
				state.next(Impl::Phase::Seal);
				break;
			}
			case Impl::Phase::Seal:
				if (!budget.rows)
					return;
				--budget.rows;
				state.physical = finish(state.physical_hash);
				state.output->seal();
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
bool PageBuilder::done() const noexcept {
	return impl_ && !impl_->failed && impl_->phase == Impl::Phase::Done;
}
std::shared_ptr<const distribution::OwnedBuffer> PageBuilder::bytes() const {
	if (!done())
		throw std::logic_error("paged page is not sealed");
	return impl_->output;
}
p::Digest PageBuilder::logical() const {
	if (!done())
		throw std::logic_error("paged page is not sealed");
	return impl_->header.logical;
}
p::Digest PageBuilder::physical() const {
	if (!done())
		throw std::logic_error("paged page is not sealed");
	return impl_->physical;
}
bool PageBuilder::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
