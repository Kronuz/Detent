#include "detent_local/disk_ledger.hh"
#include <cstdlib>
#include <iostream>
#include <new>

namespace l = detent::local;
namespace {
thread_local bool watching = false;
thread_local std::size_t allocations = 0;
void check(bool value) {
	if (!value)
		throw std::runtime_error("disk ledger invariant failed");
}
template <class Action> void rejects(Action action) {
	bool rejected = false;
	try {
		action();
	} catch (const std::exception &) {
		rejected = true;
	}
	check(rejected);
}
void same(l::DiskStats a, l::DiskStats b) {
	check(a.used == b.used && a.reserved == b.reserved && a.peak == b.peak &&
		  a.initialized == b.initialized && a.fenced == b.fenced);
}
} // namespace
namespace detent::test {
struct AllocationProbe {
	static void allocate(std::size_t) {
		if (watching)
			++allocations;
	}
};
} // namespace detent::test
#include "support/allocation_probe.hh"
int main() {
	try {
		// A generation's image, `.read` and `.live`, its empty `.lease`, and the table staged beside `selected`.
		constexpr std::uint64_t table = 64 + 137 + 255 + 32;
		check(l::DiskLedger::generation_peak(192, 0, 160) == l::DiskUsage{192 + 160 + table, 5, 1});
		check(l::DiskLedger::paged_peak({}, 1184) == l::DiskUsage{320 + 192 + 1184 + table, 5, 1});
		const std::array<std::uint64_t, 2> packs{1000, l::paged::segment_bytes};
		const auto paged_peak = l::DiskLedger::paged_peak(packs, 1328);
		check(paged_peak == l::DiskUsage{320 + 192 + 1328 + table + 1000 + l::paged::segment_bytes + 256, 9, 1});
		for (const auto invalid :
			 {std::uint64_t{0}, std::uint64_t{l::paged::segment_bytes + 1}, UINT64_MAX}) {
			const std::array lengths{invalid};
			rejects([&] { l::DiskLedger::paged_peak(lengths, 1184); });
		}
		const auto maximum = std::numeric_limits<std::uint64_t>::max();
		rejects([&] { l::DiskLedger::generation_peak(maximum, 0, 0); });
		rejects([&] { l::DiskLedger::generation_peak(191, 0, 0); });
		rejects([&] { l::DiskLedger::generation_peak(192, (1u << 16) + 1, 0); });
		check(l::DiskLedger::generation_peak(maximum - table, 0, 0).bytes == maximum);
		for (unsigned dimension = 0; dimension < 3; ++dimension) {
			l::DiskLimits limits{paged_peak.bytes, paged_peak.entries, paged_peak.generations};
			if (dimension == 0)
				--limits.bytes;
			if (dimension == 1)
				--limits.entries;
			if (dimension == 2)
				--limits.generations;
			l::DiskLedger ledger(limits);
			ledger.initialize({});
			const auto before = ledger.stats();
			rejects([&] { ledger.reserve(paged_peak); });
			same(before, ledger.stats());
		}
		// The census replaces `used`; a reservation holds its peak until it settles.
		const auto peak = l::DiskLedger::generation_peak(1000, 0, 160);
		l::DiskLedger ledger({4000, 20, 4});
		rejects([&] { ledger.reserve(peak); });
		ledger.initialize({64, 2, 0});
		rejects([&] { ledger.initialize({}); });
		auto first = ledger.reserve(peak);
		check(ledger.stats().used == l::DiskUsage{64, 2, 0} && ledger.stats().reserved == peak);
		ledger.settle(first, l::DiskUsage{1224, 6, 1});
		check(ledger.stats().used == l::DiskUsage{1224, 6, 1} && ledger.stats().reserved == l::DiskUsage{});
		rejects([&] { ledger.settle(first); });
		auto second = ledger.reserve(peak);
		check(ledger.stats().peak == l::DiskUsage{1224 + peak.bytes, 11, 2});
		ledger.settle(second, l::DiskUsage{2384, 10, 2});
		ledger.observe({1224, 6, 1}); // A collection removed the superseded generation.
		check(ledger.stats().used == l::DiskUsage{1224, 6, 1} && ledger.stats().peak.bytes == 1224 + peak.bytes);
		auto cancelled = ledger.reserve(peak);
		ledger.settle(cancelled); // Nothing written: the census stands.
		check(ledger.stats().used == l::DiskUsage{1224, 6, 1} && ledger.stats().reserved == l::DiskUsage{});
		l::DiskLedger registry({100, 100, 100});
		registry.initialize({});
		auto a = registry.reserve({1, 1, 1}), b = registry.reserve({1, 1, 1});
		rejects([&] { registry.reserve({1, 1, 1}); });
		rejects([&] { registry.settle({a.serial, 99}); });
		registry.settle(a);
		auto reused = registry.reserve({1, 1, 1});
		check(reused.slot == a.slot && reused.serial != a.serial);
		rejects([&] { registry.settle(a); });
		registry.settle(reused);
		registry.settle(b, l::DiskUsage{});
		check(registry.stats().used == l::DiskUsage{} && registry.stats().reserved == l::DiskUsage{});
		l::DiskLedger debt({1, 1, 1});
		debt.initialize({maximum, maximum, maximum});
		check(debt.stats().used == l::DiskUsage{maximum, maximum, maximum});
		rejects([&] { debt.reserve({1, 1, 1}); });
		debt.observe({});
		auto after_debt = debt.reserve({1, 1, 1});
		debt.settle(after_debt);
		l::DiskLedger fenced({100, 100, 100});
		fenced.initialize({});
		auto started = fenced.reserve({10, 1, 1});
		fenced.fence();
		rejects([&] { fenced.reserve({1, 1, 1}); });
		rejects([&] { fenced.observe({}); });
		fenced.settle(started); // A fenced store still releases its reservation.
		check(fenced.stats().reserved == l::DiskUsage{});
		l::DiskLedger allocation_free({1000, 1000, 1000});
		allocations = 0;
		watching = true;
		void *volatile probe = ::operator new(32);
		::operator delete(probe);
		watching = false;
		check(allocations > 0);
		allocations = 0;
		watching = true;
		allocation_free.initialize({});
		for (unsigned turn = 0; turn < 100000; ++turn) {
			auto permit = allocation_free.reserve({1, 1, 1});
			allocation_free.settle(permit, l::DiskUsage{1, 1, 1});
			allocation_free.observe({});
		}
		watching = false;
		check(allocations == 0 && allocation_free.stats().used == l::DiskUsage{});
		std::cout << "three-dimensional admission, census settlement, peak overlap, retained debt and 100000 "
					 "allocation-free cycles passed\n";
	} catch (const std::exception &error) {
		watching = false;
		std::cerr << error.what() << '\n';
		return 1;
	}
}
