#include "dense_graph_fixture.hh"
#include "paged_page_validator.hh"
#include <iostream>
using namespace dense_graph_test;
namespace {
void run(l::detail::PageValidator &validator) {
	for (std::size_t steps = 0; !validator.done(); ++steps) {
		check(steps < 2000000);
		l::ValidationBudget budget{1, 1};
		validator.step(budget);
		check(budget.bytes <= 1 && budget.rows <= 1);
	}
}
void validate(const std::shared_ptr<Fixture> &source, const Page &page, bool compare = true) {
	l::detail::PageValidator validator(source->account, source, page.bytes, page.reference, {},
	                                   compare);
	run(validator);
	check(validator.header().digest == page.reference.digest);
}
} // namespace
int main() {
	for (auto count : {1u, 32u, 256u, 1000u}) {
		auto source = fixture(count);
		for (const auto &page : source->pages) {
			const auto before = p::hash_counts();
			validate(source, page);
			const auto after = p::hash_counts();
			const auto header = p::decode_page_header(page.bytes, p::Limits{});
			check(after.page_calls == before.page_calls + 1 &&
			      after.page_bytes == before.page_bytes + page.bytes.size() - 32 -
			                              p::placement_bytes * header.externals);
			validate(source, page, false);
			check(p::hash_counts().page_calls == after.page_calls);
			for (auto field : {8u, 16u, 24u, 32u}) {
				auto wrong = page;
				wrong.bytes[field] ^= 1;
				rejects<std::invalid_argument>([&] { validate(source, wrong); });
			}
			auto bad = page;
			bad.bytes.back() ^= 1;
			rejects<std::invalid_argument>([&] { validate(source, bad); });
			// A reference whose summary disagrees with a self-consistent page is refused even
			// though the page's own replay passes.
			for (const bool height : {false, true}) {
				auto forged = page;
				if (height)
					++forged.reference.height;
				else
					++forged.reference.keys;
				rejects<std::invalid_argument>([&] { validate(source, forged); });
			}
		}
	}
	{
		auto source = fixture(1000);
		const auto &page = source->pages.back();
		auto account = std::make_shared<d::StorageLedger>(4 * p::segment_bytes);
		std::size_t initial = 0, peak = 0;
		{
			l::detail::PageValidator validator(account, source, page.bytes, page.reference);
			initial = account->used();
			run(validator);
			peak = account->peak();
		}
		check(!account->used() && peak > initial);
		auto short_initial = std::make_shared<d::StorageLedger>(initial - 1);
		rejects<d::StoragePressure>([&] {
			l::detail::PageValidator validator(short_initial, source, page.bytes, page.reference);
		});
		check(!short_initial->used());
		auto short_frontier = std::make_shared<d::StorageLedger>(peak - 1);
		{
			l::detail::PageValidator validator(short_frontier, source, page.bytes, page.reference);
			rejects<d::StoragePressure>([&] { run(validator); });
			l::ValidationBudget budget;
			check(validator.discard(budget));
		}
		check(!short_frontier->used());
	}
	// A checksum-correct page whose root branch names its first two inline children in swapped
	// slots keeps its bytes in preorder but is refused: readers follow slots.
	{
		auto source = fixture(1000);
		bool swapped = false;
		for (auto page : source->pages) {
			const auto node = p::decode_node(page.bytes, p::page_header_bytes, p::Limits{});
			if (node.kind != p::PageKind::Branch || (node.slot(0) & 0x8000) ||
			    (node.slot(1) & 0x8000))
				continue;
			const auto at = node.slots.data() - page.bytes.data();
			std::swap_ranges(page.bytes.begin() + at, page.bytes.begin() + at + 2,
			                 page.bytes.begin() + at + 2);
			auto header = p::decode_page_header(page.bytes, p::Limits{});
			header.digest = page.reference.digest = p::page_commitment(page.bytes, p::Limits{});
			const auto encoded = p::encode_page_header(header, p::Limits{});
			std::copy(encoded.begin(), encoded.end(), page.bytes.begin());
			rejects<std::invalid_argument>([&] { validate(source, page); });
			swapped = true;
			break;
		}
		check(swapped);
	}
	// A checksum-correct page with two tiny external leaves is structurally valid
	// but violates the height packer's boundary decisions.
	auto source = fixture(1);
	auto original = source->pages.back();
	const auto node = p::decode_node(original.bytes, p::page_header_bytes, p::Limits{});
	check(node.kind == p::PageKind::Branch && node.labels.size() == 2);
	p::PageHeader header;
	header.length = 64 + 9 + 2 * p::child_bytes;
	header.externals = 2;
	header.nodes = 1;
	header.keys = 2;
	header.height = 2;
	Page malformed;
	malformed.bytes.resize(header.length);
	const auto encoded = p::encode_page_header(header, p::Limits{});
	std::copy(encoded.begin(), encoded.end(), malformed.bytes.begin());
	malformed.bytes[64] = 1;
	malformed.bytes[65] = 0;
	malformed.bytes[66] = 1;
	malformed.bytes[67] = 'K';
	malformed.bytes[68] = 'W';
	l::write_integer(malformed.bytes, 69, 0x8000, 2);
	l::write_integer(malformed.bytes, 71, 0x8001, 2);
	for (unsigned i = 0; i < 2; ++i) {
		auto r = original.reference;
		r.length = 100;
		r.keys = 1;
		r.height = 1;
		const auto content = p::encode_content(r);
		const auto placement = p::encode_placement(r);
		std::copy(content.begin(), content.end(),
		          malformed.bytes.begin() + 73 + p::content_bytes * i);
		std::copy(placement.begin(), placement.end(),
		          malformed.bytes.end() - 40 + p::placement_bytes * i);
	}
	header.digest = p::page_commitment(malformed.bytes, p::Limits{});
	const auto committed = p::encode_page_header(header, p::Limits{});
	std::copy(committed.begin(), committed.end(), malformed.bytes.begin());
	malformed.reference = original.reference;
	malformed.reference.length = header.length;
	malformed.reference.keys = header.keys;
	malformed.reference.height = header.height;
	malformed.reference.digest = header.digest;
	p::verify_page(malformed.bytes, malformed.reference, p::Limits{});
	rejects<std::invalid_argument>([&] { validate(source, malformed); });
	std::cout << "dense incremental SHA, canonical boundaries and summary "
	             "binding passed\n";
}
