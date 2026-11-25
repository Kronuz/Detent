// The relay's page window and Merkle diff (dense distribution D7.3, DD4).
//   test_page_window                      hermetic: seal order, the diff against the oracle, the window
//   test_page_window CORPUS DIR SPEC...   A1: per SPEC[=PAGES], the diff of DIR/SPEC.patch's target
//                                         against CORPUS's build is the model's DIR/SPEC.wire
#include "delta_scenarios.hh"
#include "page_window.hh"
#include <sha256.h>

using namespace delta_test;
namespace r = detent::relay;

static std::vector<p::Reference> pages_of(const r::MerkleDiff &diff) { return diff.pages; }
static std::uint64_t bytes_of(std::span<const p::Reference> pages) {
	std::uint64_t total = 0;
	for (const auto &page : pages)
		total += page.length;
	return total;
}
static std::string hex(std::span<const char> bytes) {
	static constexpr char digits[] = "0123456789abcdef";
	std::string out;
	for (unsigned char c : bytes)
		out += {digits[c >> 4], digits[c & 15]};
	return out;
}

// The diff names exactly the oracle's occurrence writes, in seal order.
static r::MerkleDiff check_diff(const Built &base, const Built &target) {
	auto diff = r::merkle_diff(target.root(), target.pages(), base.root(), base.pages(), target.limits());
	require(diff.pages == occurrence_writes(base, target));
	require(diff.bytes == bytes_of(diff.pages));
	return diff;
}

static void hermetic() {
	const auto rows = synthetic(30000);
	const Built base(rows, 'b');
	// Against the empty base, the diff is the full copy in seal order: the build's own file order.
	const auto order = placement_order(base);
	require(order.size() > 100);
	require(std::ranges::any_of(order, [](const auto &page) { return page.length > p::page_capacity; }));
	auto full = r::merkle_diff(base.root(), base.pages(), {}, {}, base.limits());
	require(full.pages == order && full.bytes == base.page_bytes() && full.base_pages == 0);
	// A tree against itself sends nothing; the empty target sends nothing.
	require(r::merkle_diff(base.root(), base.pages(), base.root(), base.pages(), base.limits()).pages.empty());
	require(r::merkle_diff({}, {}, base.root(), base.pages(), base.limits()).pages.empty());

	struct Case {
		const char *name;
		std::size_t first, stride, updates, removed, added;
	};
	const Case cases[] = {
		{"one key", 12345, 1, 1, 0, 0},		  {"scattered", 5, 29, 900, 0, 0},
		{"clustered", 4000, 1, 2500, 0, 0},	  {"churn", 17, 31, 700, 50, 50},
		{"wide leaves", 3, 997, 25, 0, 0},	  {"every key", 0, 1, 30050, 0, 0},
	};
	for (const auto &c : cases) {
		const Built target(changed(*rows, c.first, c.stride, c.updates, c.removed, c.added), 't');
		const auto diff = check_diff(base, target);
		require(!diff.pages.empty() && diff.pages.back() == target.root());
		std::cout << "diff " << c.name << ": pages " << diff.pages.size() << " of "
				  << placement_order(target).size() << ", bytes " << diff.bytes << ", base pages read "
				  << diff.base_pages << '\n';
	}

	// The window: deltas from every held position, the segments it retains, and both bounds. These
	// generations are separate builds, so they share no segment.
	std::vector<std::unique_ptr<Built>> generations;
	generations.push_back(std::make_unique<Built>(rows, 'b'));
	for (std::size_t i = 1; i <= 4; ++i)
		generations.push_back(std::make_unique<Built>(changed(*rows, 100 * i, 37, 10 * i), char('0' + i)));
	auto held = [&](std::size_t i) {
		const auto &g = *generations[i];
		r::WindowGeneration out{10 + i, g.root(), g.pages(), nullptr, {}};
		for (const auto &segment : g.image().segments())
			out.segments.emplace_back(segment.ownership.segment, segment.ownership.length);
		return out;
	};
	auto physical = [&](std::size_t i) {
		std::uint64_t total = 0;
		for (const auto &segment : generations[i]->image().segments())
			total += segment.ownership.length;
		return total;
	};
	constexpr std::uint64_t unbounded = UINT64_MAX;
	r::PageWindow window(3);
	require(!window.head() && !window.delta(10));
	for (std::size_t i = 0; i < generations.size(); ++i) {
		window.advance(held(i), unbounded);
		require(window.head()->position == 10 + i);
	}
	require(window.size() == 3 && !window.at(10) && !window.at(11) && window.at(12));
	require(window.retained_bytes() == physical(2) + physical(3) &&
			window.held_bytes() == physical(2) + physical(3) + physical(4));
	for (std::uint64_t position = 12; position <= 14; ++position) {
		const auto delta = window.delta(position);
		require(delta && pages_of(*delta) == occurrence_writes(*generations[position - 10], *generations[4]));
	}
	require(window.delta(14)->pages.empty() && !window.delta(11));
	bool refused = false;
	try {
		window.advance(held(4), unbounded);
	} catch (const std::invalid_argument &) {
		refused = true;
	}
	require(refused);
	// A segment that several generations reach counts once, and only while the head lacks it; a head
	// smaller than what the window retains drops the oldest until it fits.
	auto made = [&](std::uint64_t position, std::initializer_list<std::pair<char, std::uint64_t>> segments) {
		r::WindowGeneration out{position, generations[0]->root(), generations[0]->pages(), nullptr, {}};
		for (const auto &[name, length] : segments) {
			l::SegmentId id{};
			id[0] = name;
			out.segments.emplace_back(id, length);
		}
		return out;
	};
	r::PageWindow shared(64);
	shared.advance(made(10, {{'a', 100}, {'b', 100}}), unbounded);
	shared.advance(made(11, {{'b', 100}, {'c', 50}}), unbounded);
	require(shared.retained_bytes() == 100 && shared.held_bytes() == 250);
	shared.advance(made(12, {{'b', 100}, {'c', 50}, {'d', 10}}), 90);
	require(shared.size() == 2 && !shared.at(10) && shared.retained_bytes() == 0 && shared.held_bytes() == 160);
	// The head's relocation replaces it: what only the old head reached is retained, within the bound.
	shared.rebase(made(12, {{'e', 140}}));
	require(shared.size() == 1 && shared.at(12) && shared.retained_bytes() == 0 && shared.held_bytes() == 140);
	shared.clear();
	require(!shared.head() && shared.retained_bytes() == 0 && shared.held_bytes() == 0);
	std::cout << "page window: ok\n";
}

// A1: the C++ diff of each model scenario is the model's transfer, page for page and byte for byte.
static void scenarios(const std::string &corpus_path, const std::string &dir, std::span<char *> specs) {
	auto started = std::chrono::steady_clock::now();
	const auto rows = corpus(corpus_path);
	const Built base(rows, 'b');
	const auto order = placement_order(base);
	auto full = r::merkle_diff(base.root(), base.pages(), {}, {}, base.limits());
	require(full.pages == order);
	std::cout << "base: " << rows->size() << " records, " << order.size() << " pages, root "
			  << hex(base.root().digest) << "; seal order equals the build's; " << seconds(started) << " s\n";
	for (const std::string arg : specs) {
		started = std::chrono::steady_clock::now();
		const auto spec = arg.substr(0, arg.find('='));
		const Built target(patched(*rows, dir + "/" + spec + ".patch"), 't');
		const auto built = seconds(started);
		started = std::chrono::steady_clock::now();
		const auto diff = r::merkle_diff(target.root(), target.pages(), base.root(), base.pages(), target.limits());
		const auto diffed = seconds(started);
		const auto stream = wire_form(diff.pages, target.pages(), target.limits());
		const auto model = load(dir + "/" + spec + ".wire");
		const bool same = stream == model;
		SHA256 hash;
		hash.add(stream.data(), stream.size());
		std::cout << "delta " << spec << ": pages " << diff.pages.size() << ", page bytes " << diff.bytes
				  << ", base pages read " << diff.base_pages << ", wire raw " << stream.size() << ", sha256 " << hash.getHash() << ", model wire "
				  << model.size() << (same ? " identical" : " DIFFERENT") << ", root " << hex(target.root().digest)
				  << "; build " << built << " s, diff " << diffed << " s\n";
		require(same);
		if (arg.size() > spec.size())
			require(diff.pages.size() == std::stoull(arg.substr(spec.size() + 1)));
	}
}

int main(int argc, char **argv) {
	report_uncaught_exceptions();
	if (argc == 1)
		hermetic();
	else {
		require(argc >= 4);
		scenarios(argv[1], argv[2], std::span(argv + 3, argc - 3));
	}
}
