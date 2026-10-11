#include "paged_segment_builder.hh"
#include <iostream>
#include <stdexcept>

namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("segment packing invariant");
}
template <class F> void rejects(F call) {
	try {
		call();
	} catch (const std::logic_error &) {
		return;
	}
	throw std::runtime_error("unsealed segment accepted");
}
void copy(l::detail::SegmentBuilder &builder) {
	unsigned steps = 0;
	while (!builder.page_ready()) {
		l::ValidationBudget budget{1, 1};
		builder.step(budget);
		check(budget.bytes <= 1 && budget.rows <= 1 && ++steps < 100000);
	}
}
} // namespace
int main() {
	auto account = std::make_shared<d::StorageLedger>(4 * p::segment_bytes);
	l::StorageIdentity storage{}, publication{};
	storage.fill('s');
	publication.fill('g');
	std::size_t admitted;
	{
		l::detail::SegmentBuilder segment(account, storage, publication, 0);
		admitted = account->used();
		rejects([&] { segment.seal(); });
		rejects([&] { segment.bytes(); });
		std::array<char, 8> body{};
		l::write_integer(body, 0, 9, 8);
		l::detail::PageBuilder page(account, p::PageKind::Namespace, 1, "x", {}, body);
		while (!page.done()) {
			l::ValidationBudget budget;
			page.step(budget);
		}
		const auto page_length = page.bytes()->size();
		segment.append(page);
		rejects([&] { segment.reference(); });
		copy(segment);
		const auto first = segment.reference();
		check(first.offset == 0 && first.length == page_length);
		segment.append(page);
		l::ValidationBudget disposal{0, 1};
		check(page.discard(disposal));
		copy(segment);
		const auto second = segment.reference();
		check(second.offset == (page_length + 7) / 8 * 8 && second.segment == first.segment);
		segment.seal();
		l::ValidationBudget empty{0, 0};
		segment.step(empty);
		check(!segment.done());
		l::ValidationBudget seal{0, 1};
		segment.step(seal);
		check(segment.done() && !seal.rows);
		auto bytes = segment.bytes();
		const auto ownership = segment.ownership();
		check(bytes->sealed() && bytes->size() == second.offset + second.length);
		check(ownership.length == bytes->size() && ownership.storage == storage);
		check(ownership.checksum == p::physical_commitment({bytes->data(), bytes->size()}));
		check(l::decode_segment_ownership(l::encode_segment_ownership(ownership)) == ownership);
		p::verify_page({bytes->data() + first.offset, first.length}, first);
		p::verify_page({bytes->data() + second.offset, second.length}, second);
		for (auto i = page_length; i < second.offset; ++i)
			check(bytes->data()[i] == 0);
		check(!segment.discard(empty));
		disposal.rows = 1;
		check(segment.discard(disposal) && account->used() > 0);
		bytes.reset();
		check(!account->used());
	}
	auto short_account = std::make_shared<d::StorageLedger>(admitted - 1);
	try {
		l::detail::SegmentBuilder segment(short_account, storage, publication, 0);
		throw std::runtime_error("short admission accepted");
	} catch (const d::StoragePressure &) {
	}
	check(!short_account->used());
	{
		std::array<char, 8> body{};
		l::detail::PageBuilder page(account, p::PageKind::Namespace, 1, {}, {}, body);
		while (!page.done()) {
			l::ValidationBudget budget;
			page.step(budget);
		}
		l::detail::SegmentBuilder first(account, storage, publication, 0);
		l::detail::SegmentBuilder next(account, storage, publication, 1);
		next.append(page);
		copy(next);
		unsigned count = 0;
		while (first.fits(page)) {
			first.append(page);
			while (!first.page_ready()) {
				l::ValidationBudget budget;
				first.step(budget);
			}
			++count;
		}
		check(count == p::segment_bytes / page.bytes()->size());
		check(first.reference().segment != next.reference().segment);
		first.seal();
		l::ValidationBudget budget;
		first.step(budget);
		check(first.bytes()->size() == count * page.bytes()->size());
		rejects([&] { first.append(page); });
	}
	check(!account->used());
	std::cout << "aligned segment packing, immutable ownership, bounded copies and admission passed\n";
}
