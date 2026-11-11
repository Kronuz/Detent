// Read-path cost of the in-page walks: warm PagedReadState lookups (every row, fixed shuffled
// order) and verify_page_structure over every page. Prints one line per round.
//   dense_walk_bench [ROWS] [ROUNDS]
#include "dense_graph_fixture.hh"
#include "paged_read_state.hh"
#include <chrono>
#include <cstdio>
#include <numeric>
#include <random>
#include <string>
using namespace dense_graph_test;
namespace x = l::detail;
int main(int argc, char **argv) {
	const std::size_t rows = argc > 1 ? std::stoul(argv[1]) : 20000;
	const unsigned rounds = argc > 2 ? unsigned(std::stoul(argv[2])) : 5;
	const auto f = fixture(rows);
	const x::PagedReadState state{f->root, f->inputs};
	std::vector<std::size_t> order(f->rows.size());
	std::iota(order.begin(), order.end(), 0);
	std::shuffle(order.begin(), order.end(), std::mt19937(1));
	std::vector<p::PageKind> kinds;
	for (const auto &row : f->rows)
		kinds.push_back(p::decode_key(row.key).kind);
	using clock = std::chrono::steady_clock;
	std::uint64_t sink = 0;
	for (unsigned round = 0; round <= rounds; ++round) {
		const auto start = clock::now();
		for (unsigned pass = 0; pass != 20; ++pass)
			for (const auto i : order)
				sink += state.lookup(f->rows[i].key, kinds[i])->size();
		const auto middle = clock::now();
		for (unsigned pass = 0; pass != 20; ++pass)
			for (const auto &page : f->pages)
				p::verify_page_structure(page.bytes, page.reference, p::Limits{});
		const auto end = clock::now();
		if (!round)
			continue;
		const auto ns = [](auto a, auto b) { return std::chrono::duration<double, std::nano>(b - a).count(); };
		std::printf("round=%u rows=%zu pages=%zu lookup_ns=%.2f verify_ns_per_page=%.1f\n", round,
					order.size(), f->pages.size(), ns(start, middle) / (20.0 * order.size()),
					ns(middle, end) / (20.0 * f->pages.size()));
	}
	std::fprintf(stderr, "sink=%llu\n", static_cast<unsigned long long>(sink));
}
