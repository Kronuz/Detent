#include "paged_page_builder.hh"
#include <iostream>
#include <stdexcept>

namespace d = detent::distribution;
namespace p = detent::local::paged;
namespace l = detent::local;
namespace {
void check(bool ok) {
	if (!ok)
		throw std::runtime_error("budgeted page builder invariant");
}
template <class Error, class F> void rejects(F call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid page builder operation accepted");
}
std::size_t qualify(p::PageKind kind, std::uint64_t keys, std::string_view prefix,
					std::span<const p::Child> children, std::span<const char> body) {
	auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
	std::size_t peak;
	{
		l::detail::PageBuilder builder(account, kind, keys, prefix, children, body);
		rejects<std::logic_error>([&] { builder.bytes(); });
		l::ValidationBudget empty{0, 0};
		builder.step(empty);
		check(!builder.done());
		unsigned steps = 0;
		while (!builder.done()) {
			l::ValidationBudget budget{1, 1};
			builder.step(budget);
			check(budget.bytes <= 1 && budget.rows <= 1 && ++steps < 250000);
		}
		check(builder.logical() == p::logical_commitment(kind, prefix, children, body));
		auto bytes = builder.bytes();
		check(bytes->sealed());
		check(builder.physical() == p::physical_commitment({bytes->data(), bytes->size()}));
		p::Reference reference;
		reference.segment.fill('s');
		reference.length = bytes->size();
		reference.physical = builder.physical();
		reference.logical = builder.logical();
		p::verify_page({bytes->data(), bytes->size()}, reference);
		peak = account->peak();
		check(!builder.discard(empty));
		bytes.reset();
		l::ValidationBudget disposal{0, 1};
		check(builder.discard(disposal) && !disposal.rows && !builder.done());
		check(!account->used());
	}
	auto short_account = std::make_shared<d::StorageLedger>(peak - 1);
	rejects<d::StoragePressure>(
		[&] { l::detail::PageBuilder builder(short_account, kind, keys, prefix, children, body); });
	check(!short_account->used());
	return peak;
}
} // namespace
int main() {
	std::array<char, 8> watermark{};
	l::write_integer(watermark, 0, 17, 8);
	qualify(p::PageKind::Namespace, 1, std::string_view("Wns\0", 4), {}, watermark);
	std::array<p::Child, 256> children;
	for (unsigned i = 0; i < children.size(); ++i) {
		auto &child = children[i];
		child.label = i;
		child.reference.segment.fill('s');
		child.reference.segment[0] = static_cast<char>(i);
		child.reference.physical.fill('p');
		child.reference.logical.fill('l');
		child.reference.length = p::page_header_bytes + 8;
	}
	std::string prefix(512, 'x');
	const auto peak = qualify(p::PageKind::Branch, 256, prefix, children, {});
	auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
	{
		children[1].label = 0;
		l::detail::PageBuilder bad(account, p::PageKind::Branch, 256, prefix, children, {});
		rejects<std::invalid_argument>([&] {
			while (!bad.done()) {
				l::ValidationBudget budget;
				bad.step(budget);
			}
		});
		rejects<std::logic_error>([&] { bad.bytes(); });
		l::ValidationBudget disposal{0, 1};
		check(bad.discard(disposal));
	}
	check(!account->used());
	p::Limits small;
	small.prefix_bytes = 3;
	rejects<std::invalid_argument>([&] {
		l::detail::PageBuilder bad(account, p::PageKind::Namespace, 1, std::string_view("Wns\0", 4), {},
								   watermark, small);
	});
	check(!account->used());
	small.prefix_bytes = 4;
	small.bytes = p::page_header_bytes + 4 + watermark.size() - 1;
	rejects<std::invalid_argument>([&] {
		l::detail::PageBuilder bad(account, p::PageKind::Namespace, 1, std::string_view("Wns\0", 4), {},
								   watermark, small);
	});
	check(!account->used());
	rejects<std::invalid_argument>(
		[&] { l::detail::PageBuilder bad(account, p::PageKind::Overflow, 0, {}, {}, watermark); });
	check(!account->used());
	std::cout
		<< "one-byte page construction, canonical hashes, terminal failure and exact admission passed; peak="
		<< peak << '\n';
}
