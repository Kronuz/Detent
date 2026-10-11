#include "paged_page_builder.hh"
#include "paged_page_validator.hh"
#include <iostream>
#include <stdexcept>

namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("incremental page validation invariant");
}
template <class Error, class F> void rejects(F call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid page accepted");
}
void run(l::detail::PageValidator &validator) {
	unsigned steps = 0;
	while (!validator.done()) {
		l::ValidationBudget budget{1, 1};
		validator.step(budget);
		check(budget.bytes <= 1 && budget.rows <= 1 && ++steps < 200000);
	}
}
void qualify(p::PageKind kind, std::string_view prefix, std::span<const p::Child> children,
			 std::span<const char> body) {
	auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
	l::detail::PageBuilder builder(account, kind, children.empty() ? 1 : children.size(), prefix, children,
								   body);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto bytes = builder.bytes();
	p::Reference reference;
	reference.segment.fill('s');
	reference.length = bytes->size();
	reference.physical = builder.physical();
	reference.logical = builder.logical();
	l::ValidationBudget disposal{0, 1};
	check(builder.discard(disposal));
	const auto baseline = account->used();
	std::size_t admitted;
	{
		l::detail::PageValidator validator(account, bytes, {bytes->data(), bytes->size()}, reference);
		admitted = account->used() - baseline;
		rejects<std::logic_error>([&] { validator.header(); });
		l::ValidationBudget empty{0, 0};
		validator.step(empty);
		check(!validator.done());
		run(validator);
		check(validator.header().kind == kind && validator.header().prefix == prefix.size());
		check(!validator.discard(empty));
		disposal.rows = 1;
		check(validator.discard(disposal) && account->used() == baseline);
	}
	auto short_account = std::make_shared<d::StorageLedger>(admitted - 1);
	rejects<d::StoragePressure>([&] {
		l::detail::PageValidator validator(short_account, bytes, {bytes->data(), bytes->size()}, reference);
	});
	check(!short_account->used());
	auto damaged = d::make_owned_buffer(account, bytes->size());
	damaged->append(bytes->view());
	damaged->mutable_bytes().back() ^= 1;
	damaged->seal();
	{
		l::detail::PageValidator validator(account, damaged, {damaged->data(), damaged->size()}, reference);
		rejects<std::invalid_argument>([&] { run(validator); });
		check(!validator.done());
		l::ValidationBudget budget;
		rejects<std::logic_error>([&] { validator.step(budget); });
	}
	reference.physical = p::physical_commitment({damaged->data(), damaged->size()});
	{
		l::detail::PageValidator validator(account, damaged, {damaged->data(), damaged->size()}, reference);
		rejects<std::invalid_argument>([&] { run(validator); });
	}
	damaged.reset();
	bytes.reset();
	check(!account->used());
}
} // namespace
int main() {
	std::array<char, 8> body{};
	l::write_integer(body, 0, 7, 8);
	qualify(p::PageKind::Namespace, std::string_view("Wns\0", 4), {}, body);
	std::array<p::Child, 256> children{};
	for (unsigned i = 0; i < children.size(); ++i) {
		auto &child = children[i];
		child.label = i;
		child.reference.segment.fill('s');
		child.reference.physical.fill('p');
		child.reference.logical.fill('l');
		child.reference.length = p::page_header_bytes + 8;
	}
	qualify(p::PageKind::Branch, std::string(512, 'x'), children, {});
	qualify(p::PageKind::Definition, {}, {}, std::string(33816, 'v'));
	std::cout << "incremental page checks, terminal corruption and exact admission passed\n";
}
