// LazyPageAccess's lock-free segment table and verified-page bitmap (reader design D3).
#include "lazy_page_access.hh"
#include "paged_read_state.hh"
#include "prepared_reader_fixture.hh"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <random>
#include <thread>
#include <unistd.h>
#include <vector>

namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
namespace x = l::detail;
namespace {
using prepared_test::check;
using prepared_test::Fixture;
using Handle = d::RetirementHandle<x::LazyPageAccess>;
constexpr unsigned threads = 16;

struct Harness {
	std::shared_ptr<Fixture> fixture;
	d::RetirementRuntime runtime;
	d::RetirementDomain domain;
	std::shared_ptr<d::StorageLedger> mappings = std::make_shared<d::StorageLedger>(256 * p::segment_bytes);
	Handle access;
	explicit Harness(std::shared_ptr<Fixture> shared, l::ViewLimits limits = big())
	    : fixture(std::move(shared)), runtime(fixture->heap), domain(runtime.domain(fixture->heap)),
	      access(domain.make<x::LazyPageAccess>(fixture, fixture->directory, ::getuid(), fixture->storage,
	                                            fixture->root, limits, domain.factory(), fixture->heap,
	                                            mappings)) {}
	~Harness() {
		access.reset();
		domain.close().wait();
	}
	static l::ViewLimits big() {
		l::ViewLimits limits;
		limits.bytes = 64 * p::segment_bytes;
		return limits;
	}
	// Sequential model: the bytes the builder wrote for this reference.
	std::span<const char> expected(const p::Reference &reference) const {
		for (const auto &segment : fixture->segments)
			if (segment.ownership.segment == reference.segment)
				return segment.bytes.subspan(reference.offset, reference.length);
		throw std::runtime_error("reference outside the fixture");
	}
	x::PagedReadState state() const {
		auto owner = std::make_shared<Handle>(access);
		return {fixture->root, {}, std::shared_ptr<const x::PagedPageAccess>(owner, owner->get())};
	}
};
bool refused(const x::LazyPageAccess &access, const p::Reference &reference) {
	try {
		access.resolve(reference);
	} catch (const x::ReadSourceFailure &) {
		return true;
	}
	return false;
}
template <class Body> void parallel(unsigned count, Body body) {
	std::atomic<unsigned> ready{0};
	std::vector<std::thread> pool;
	std::atomic<bool> error{false};
	for (unsigned t = 0; t != count; ++t)
		pool.emplace_back([&, t] {
			ready.fetch_add(1);
			while (ready.load() != count)
				std::this_thread::yield();
			try {
				body(t);
			} catch (const std::exception &failure) {
				std::fprintf(stderr, "thread %u: %s\n", t, failure.what());
				error = true;
			}
		});
	for (auto &thread : pool)
		thread.join();
	check(!error);
}

// Holds the first two arrivals at one window until both are there, so both race past it.
struct Rendezvous {
	x::LazyPageWindow at;
	std::atomic<unsigned> arrived{0};
	std::atomic<bool> met{false};
	static void hook(x::LazyPageWindow window, void *context) noexcept {
		auto self = static_cast<Rendezvous *>(context);
		if (window != self->at || self->arrived.fetch_add(1) >= 2)
			return;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (self->arrived.load() < 2 && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		if (self->arrived.load() >= 2)
			self->met = true;
	}
};

void stress() {
	constexpr unsigned namespaces = 26;
	Harness harness(std::make_shared<Fixture>(namespaces));
	const auto &access = *harness.access;
	auto references = harness.fixture->definitions;
	references.push_back(harness.fixture->root.tree);
	std::vector<std::atomic<const char *>> seen(references.size());
	auto round = [&](unsigned t, unsigned repetitions) {
		std::mt19937 random(t * 7919 + repetitions);
		std::vector<std::size_t> order(references.size());
		for (std::size_t i = 0; i != order.size(); ++i)
			order[i] = i;
		const auto state = harness.state();
		for (unsigned r = 0; r != repetitions; ++r) {
			std::shuffle(order.begin(), order.end(), random);
			for (const auto i : order) {
				const auto bytes = access.resolve(references[i]);
				const auto model = harness.expected(references[i]);
				check(bytes.size() == model.size() && std::memcmp(bytes.data(), model.data(), model.size()) == 0);
				const char *first = nullptr;
				// Every thread gets the one published mapping of a segment.
				if (!seen[i].compare_exchange_strong(first, bytes.data()))
					check(first == bytes.data());
			}
			const std::string name(1, char('a' + (r * threads + t) % namespaces));
			const auto found = state.definition(name, "k");
			check(found && (name == "a" ? found->default_int() == 42 : found->default_bool() == true));
			check(!state.definition(name, "missing"));
		}
	};
	parallel(threads, [&](unsigned t) { round(t, 2); });
	// The same work on one thread verifies each page it touches exactly once.
	Harness serial(harness.fixture);
	{
		const auto state = serial.state();
		for (const auto &reference : references)
			serial.access->resolve(reference);
		for (unsigned i = 0; i != namespaces; ++i) {
			check(bool(state.definition(std::string(1, char('a' + i)), "k")));
			check(!state.definition(std::string(1, char('a' + i)), "missing"));
		}
	}
	const auto pages = serial.access->page_verifications();
	check(pages == serial.fixture->root.pages && serial.access->mapped_segments() == serial.fixture->segments.size());
	check(access.mapped_segments() == harness.fixture->segments.size());
	const auto first = access.page_verifications();
	check(first >= pages && first <= pages * threads);
	std::printf("stress: %llu verifications for %llu pages (%llu concurrent duplicates)\n",
	            (unsigned long long)first, (unsigned long long)pages, (unsigned long long)(first - pages));
	parallel(threads, [&](unsigned t) { round(t + threads, 64); });
	check(access.page_verifications() == first);
	check(access.mapped_segments() == harness.fixture->segments.size());
	check(access.available());
}

void publish_window() {
	Harness harness(std::make_shared<Fixture>());
	Rendezvous meet{x::LazyPageWindow::Publish};
	x::LazyPageTesting::install(&Rendezvous::hook, &meet);
	const auto reference = harness.fixture->definitions[0];
	std::array<const char *, 2> got{};
	parallel(2, [&](unsigned t) { got[t] = harness.access->resolve(reference).data(); });
	x::LazyPageTesting::install(nullptr, nullptr);
	// Both mapped the segment; the loser dropped its copy and returned the winner's bytes.
	check(got[0] == got[1]);
	check(harness.access->mapped_segments() == 1);
	std::printf("publish window: %s\n", meet.met ? "both threads raced the publish" : "not reached");
}

void mark_window() {
	Harness harness(std::make_shared<Fixture>());
	const auto reference = harness.fixture->definitions[0];
	auto neighbour = harness.fixture->root.tree;
	harness.access->resolve(neighbour);
	check(harness.access->page_verifications() == 1);
	Rendezvous meet{x::LazyPageWindow::Mark};
	x::LazyPageTesting::install(&Rendezvous::hook, &meet);
	parallel(2, [&](unsigned) { harness.access->resolve(reference); });
	x::LazyPageTesting::install(nullptr, nullptr);
	// Both verified the page and both set its bit; neither lookup since verifies it again.
	const auto verified = harness.access->page_verifications();
	check(meet.met ? verified == 3 : verified >= 2 && verified <= 3);
	harness.access->resolve(reference);
	harness.access->resolve(neighbour);
	check(harness.access->page_verifications() == verified);
	std::printf("mark window: %s\n", meet.met ? "both threads verified one page" : "not reached");
}

// Parks one thread after its miss until the main thread has published the same segment.
struct Parked {
	std::atomic<int> state{0}; // 0 idle, 1 parked, 2 released
	static void hook(x::LazyPageWindow window, void *context) noexcept {
		auto self = static_cast<Parked *>(context);
		int idle = 0;
		if (window != x::LazyPageWindow::Miss || !self->state.compare_exchange_strong(idle, 1))
			return;
		while (self->state.load() != 2)
			std::this_thread::yield();
	}
};
// A segment published between a thread's miss and its admission check is not a new segment, even
// when the counts it reads have reached the pack count (variant 0) or the mapped-bytes cap (1).
void raced_admission(unsigned variant) {
	auto fixture = std::make_shared<Fixture>(3);
	auto limits = Harness::big();
	// The late thread misses the index segment; the main thread publishes it with two others.
	const auto target = fixture->root.tree;
	std::vector<p::Reference> published{target, fixture->definitions[0], fixture->definitions[1]};
	if (variant) {
		// A cap one byte short of every segment: the late thread's recount of the index exceeds it.
		limits.bytes = fixture->physical_bytes - 1;
		const auto size = [&](const p::Reference &reference) {
			return std::ranges::find_if(fixture->segments, [&](const auto &segment) {
				return segment.ownership.segment == reference.segment;
			})->bytes.size();
		};
		if (size(target) < size(fixture->definitions[2])) {
			std::puts("raced admission at the cap: not reached (the index is the smaller segment)");
			return;
		}
	} else {
		published.push_back(fixture->definitions[2]);
	}
	Harness harness(fixture, limits);
	Parked parked;
	x::LazyPageTesting::install(&Parked::hook, &parked);
	const char *late = nullptr;
	std::thread waiter([&] {
		try {
			late = harness.access->resolve(target).data();
		} catch (const x::ReadSourceFailure &) {
		}
	});
	while (parked.state.load() != 1)
		std::this_thread::yield();
	const auto early = harness.access->resolve(target).data();
	for (const auto &reference : published)
		harness.access->resolve(reference);
	check(harness.access->mapped_segments() == published.size());
	parked.state = 2;
	waiter.join();
	x::LazyPageTesting::install(nullptr, nullptr);
	check(late == early && harness.access->available());
	check(harness.access->mapped_segments() == published.size());
	std::printf("raced admission (%s): the late thread used the published segment\n", variant ? "cap" : "count");
}

// A page whose only damage is its header's logical digest: framing and body decode, the hash does not.
void corrupt_first_touch() {
	auto fixture = std::make_shared<Fixture>(3);
	const auto target = fixture->definitions[1];
	const x::SegmentNames names(target.segment);
	const auto path = fixture->path / names.payload.data();
	const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
	check(fd >= 0);
	char byte = 0;
	check(::pread(fd, &byte, 1, target.offset + 40) == 1);
	byte ^= 1;
	check(::pwrite(fd, &byte, 1, target.offset + 40) == 1);
	check(::close(fd) == 0);
	{
		Harness harness(fixture);
		check(!harness.access->resolve(fixture->definitions[0]).empty());
		std::atomic<unsigned> refusals{0};
		parallel(threads, [&](unsigned) {
			if (refused(*harness.access, target))
				refusals.fetch_add(1);
		});
		check(refusals == threads);
		check(!harness.access->available());
		check(refused(*harness.access, fixture->definitions[0]));
	}
	{
		// Serially, through the trie walk: the damaged definition is refused on first touch.
		Harness harness(fixture);
		const auto state = harness.state();
		check(state.definition("a", "k")->default_int() == 42);
		bool caught = false;
		try {
			state.definition("b", "k");
		} catch (const x::ReadSourceFailure &) {
			caught = true;
		}
		check(caught && !harness.access->available());
	}
}

// Every page reachable from the root, read from the builder's bytes.
std::vector<p::Reference> all_pages(const Fixture &fixture) {
	std::vector<p::Reference> pages;
	auto walk = [&](const p::Reference &reference, auto &self) -> void {
		pages.push_back(reference);
		for (const auto &segment : fixture.segments) {
			if (segment.ownership.segment != reference.segment)
				continue;
			const auto bytes = segment.bytes.subspan(reference.offset, reference.length);
			const auto header = p::decode_page_header(bytes);
			for (unsigned i = 0; i != header.externals; ++i)
				self(p::decode_external(bytes, i), self);
		}
	};
	walk(fixture.root.tree, walk);
	return pages;
}
void flip(const Fixture &fixture, const p::Reference &reference, std::size_t at) {
	const x::SegmentNames names(reference.segment);
	const int fd = ::open((fixture.path / names.payload.data()).c_str(), O_RDWR | O_CLOEXEC);
	check(fd >= 0);
	char byte = 0;
	check(::pread(fd, &byte, 1, reference.offset + at) == 1);
	byte ^= 1;
	check(::pwrite(fd, &byte, 1, reference.offset + at) == 1);
	check(::close(fd) == 0);
}
// A damaged page whose segment neighbours are already verified is still refused on first touch,
// damaged at its first byte, its logical digest, its last header byte and its last body byte.
void neighbour_corruption() {
	auto fixture = std::make_shared<Fixture>(5);
	const auto pages = all_pages(*fixture);
	unsigned cases = 0;
	for (const auto &target : pages)
		for (const std::size_t at : {std::size_t(0), std::size_t(40), std::size_t(79), std::size_t(target.length - 1)}) {
			flip(*fixture, target, at);
			{
				Harness harness(fixture);
				for (const auto &other : pages)
					if (!(other == target))
						harness.access->resolve(other);
				std::atomic<unsigned> refusals{0};
				parallel(threads, [&](unsigned) {
					if (refused(*harness.access, target))
						refusals.fetch_add(1);
				});
				check(refusals == threads && !harness.access->available());
			}
			flip(*fixture, target, at);
			++cases;
		}
	check(cases == pages.size() * 4);
	std::printf("neighbour corruption: %u cases refused\n", cases);
}
// A forged physical digest on a never-verified page is refused and fails the generation.
void forged_unverified() {
	auto fixture = std::make_shared<Fixture>(3);
	Harness harness(fixture);
	check(!harness.access->resolve(fixture->definitions[0]).empty());
	auto forged = fixture->definitions[2];
	forged.digest[0] ^= 1;
	check(refused(*harness.access, forged));
	check(!harness.access->available());
}

// A miss at the mapped-bytes cap throws and fails the generation, as before D3.
void cap_miss() {
	auto fixture = std::make_shared<Fixture>(3);
	auto limits = Harness::big();
	limits.bytes = fixture->physical_bytes - 1;
	Harness harness(fixture, limits);
	for (const auto &reference : fixture->definitions)
		check(!harness.access->resolve(reference).empty());
	check(refused(*harness.access, fixture->root.tree));
	check(!harness.access->available());
	check(refused(*harness.access, fixture->definitions[0]));
}
} // namespace

int main(int argc, char **argv) {
	// An argument runs one case: stress, publish, mark, raced, corrupt, cap, neighbour or forged.
	const std::string_view only = argc > 1 ? argv[1] : "";
	auto run = [&](std::string_view name, auto body) {
		if (only.empty() || only == name)
			body();
	};
	run("stress", stress);
	run("publish", publish_window);
	run("mark", mark_window);
	run("raced", [] {
		raced_admission(0);
		raced_admission(1);
	});
	run("corrupt", corrupt_first_touch);
	run("cap", cap_miss);
	run("neighbour", neighbour_corruption);
	run("forged", forged_unverified);
	std::puts("lazy page bitmap: ok");
}
