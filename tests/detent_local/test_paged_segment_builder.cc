#include "dense_fixture.hh"
#include "paged_segment_builder.hh"
#include <iostream>
namespace x = detent::local::detail;
namespace d = detent::distribution;
using namespace dense_test;
static x::SealedPage make_page(const std::shared_ptr<d::StorageLedger> &account) {
	auto encoded = watermark();
	auto bytes = d::make_owned_buffer(account, encoded.size());
	bytes->append({encoded.data(), encoded.size()});
	bytes->seal();
	return {bytes, reference(encoded), {}};
}
static void drain(x::SegmentBuilder &segment, std::size_t quantum = 1) {
	std::size_t turns = 0;
	while (!segment.page_ready()) {
		l::ValidationBudget budget{quantum, 1};
		segment.step(budget);
		require(++turns < 100000 && budget.bytes <= quantum && budget.rows <= 1);
	}
}
int main() {
	auto account = std::make_shared<d::StorageLedger>(4 * p::segment_bytes);
	l::StorageIdentity storage{}, publication{};
	storage.fill('s');
	publication.fill('g');
	std::size_t admitted = 0;
	{
		x::SegmentBuilder segment(account, publication, 0);
		admitted = account->used();
		auto page = make_page(account);
		p::reset_hash_counts();
		segment.append(page);
		drain(segment);
		const auto first = segment.reference();
		require(first.offset == 0 && first.length == 79 && first.keys == 1 && first.height == 1);
		segment.append(page);
		drain(segment);
		const auto second = segment.reference();
		require(second.offset == 80 && second.segment == first.segment);
		segment.seal();
		l::ValidationBudget no{0, 0};
		segment.step(no);
		require(!segment.done());
		l::ValidationBudget seal{0, 1};
		segment.step(seal);
		require(segment.done());
		require(p::hash_counts() == p::HashCounts{0, 0, 1, 159, 0, 0});
		const auto bytes = segment.bytes();
		require(bytes->size() == 159 && bytes->data()[79] == 0);
		p::verify_page({bytes->data(), 79}, first, p::Limits{});
		p::verify_page({bytes->data() + 80, 79}, second, p::Limits{});
		require(segment.ownership().checksum == p::pack_commitment({bytes->data(), bytes->size()}));
	}
	require(account->used() == 0);
	auto short_account = std::make_shared<d::StorageLedger>(admitted - 1);
	try {
		x::SegmentBuilder segment(short_account, publication, 0);
		require(false);
	} catch (const d::StoragePressure &) {
	}
	require(short_account->used() == 0);
	{
		auto page = make_page(account);
		x::SegmentBuilder segment(account, publication, 0),
			next(account, publication, 1);
		next.append(page);
		drain(next);
		std::size_t count = 0;
		while (segment.fits(page)) {
			segment.append(page);
			drain(segment, 8192);
			++count;
		}
		require(count == (p::segment_bytes - 79) / 80 + 1);
		require(segment.reference().segment != next.reference().segment);
		segment.seal();
		l::ValidationBudget budget;
		segment.step(budget);
		require(segment.bytes()->size() == (count - 1) * 80 + 79);
	}
	require(account->used() == 0);
	std::cout << "dense sealed-page segment packing, checksum accounting and quota passed\n";
}
