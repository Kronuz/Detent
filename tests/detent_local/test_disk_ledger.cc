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
void *operator new(std::size_t bytes) {
	if (watching)
		++allocations;
	if (auto pointer = std::malloc(bytes ? bytes : 1))
		return pointer;
	throw std::bad_alloc();
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
int main() {
	try {
		check(l::DiskLedger::paged_peak({}) == l::DiskUsage{816, 4, 1});
		const std::array<std::uint64_t, 2> packs{1000, l::paged::segment_bytes};
		const auto paged_peak = l::DiskLedger::paged_peak(packs);
		check(paged_peak == l::DiskUsage{816 + 1000 + l::paged::segment_bytes + 256, 8, 1});
		for (const auto invalid :
			 {std::uint64_t{0}, std::uint64_t{l::paged::segment_bytes + 1}, UINT64_MAX}) {
			const std::array lengths{invalid};
			rejects([&] { l::DiskLedger::paged_peak(lengths); });
		}
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
		const auto peak = l::DiskLedger::generation_peak(192);
		check(peak == l::DiskUsage{496, 3, 1});
		for (unsigned dimension = 0; dimension < 3; ++dimension) {
			l::DiskLimits limits{peak.bytes, peak.entries, peak.generations};
			if (dimension == 0)
				--limits.bytes;
			if (dimension == 1)
				--limits.entries;
			if (dimension == 2)
				--limits.generations;
			l::DiskLedger ledger(limits);
			ledger.initialize({});
			auto before = ledger.stats();
			rejects([&] { ledger.reserve(peak); });
			same(before, ledger.stats());
		}
		l::DiskLedger ledger({3672, 8, 2});
		rejects([&] { ledger.reserve(peak); });
		ledger.initialize({64, 2, 0}); // Storage identity and stable owner lock.
		rejects([&] { ledger.initialize({}); });
		auto first = ledger.reserve(l::DiskLedger::generation_peak(1000));
		rejects([&] { ledger.settle(first, {1304, 3, 1}); });
		check(ledger.stats().used == l::DiskUsage{64, 2, 0} &&
			  ledger.stats().reserved == l::DiskUsage{1304, 3, 1});
		ledger.accepted(first);
		rejects([&] { ledger.cancel_unstarted(first); });
		rejects([&] { ledger.settle(first, {1304, 3, 1}); });
		check(ledger.originals(first) == 1);
		ledger.completed(first);
		rejects([&] { ledger.completed(first); });
		ledger.settle(first, {1304, 3, 1});
		check(ledger.stats().used == l::DiskUsage{1368, 5, 1} && ledger.stats().reserved == l::DiskUsage{});
		rejects([&] { ledger.accepted(first); });
		auto replacement = ledger.reserve(l::DiskLedger::generation_peak(2000));
		check(ledger.stats().peak == l::DiskUsage{3672, 8, 2}); // Both descriptors still charged.
		ledger.accepted(replacement);
		ledger.completed(replacement);
		ledger.settle(replacement, {2304, 3, 1},
					  {176, 1, 0}); // Final selection barrier credits old descriptor.
		check(ledger.stats().used == l::DiskUsage{3496, 7, 2});
		ledger.reclaimed(
			{1128, 2, 1}); // Old image and lease: two barriers and handle closure proved by caller.
		check(ledger.stats().used == l::DiskUsage{2368, 5, 1});
		auto before_bad_credit = ledger.stats();
		rejects([&] { ledger.reclaimed({2369, 0, 0}); });
		same(before_bad_credit, ledger.stats());
		l::DiskLedger short_replacement({3671, 8, 2});
		short_replacement.initialize({1368, 5, 1});
		rejects([&] { short_replacement.reserve(l::DiskLedger::generation_peak(2000)); });
		check(short_replacement.stats().used == l::DiskUsage{1368, 5, 1});
		l::DiskLedger registry({100, 100, 100});
		registry.initialize({});
		auto a = registry.reserve({1, 1, 1}), b = registry.reserve({1, 1, 1});
		rejects([&] { registry.reserve({1, 1, 1}); });
		rejects([&] { registry.cancel_unstarted({a.serial, 99}); });
		registry.cancel_unstarted(a);
		auto reused = registry.reserve({1, 1, 1});
		check(reused.slot == a.slot && reused.serial != a.serial);
		rejects([&] { registry.cancel_unstarted(a); });
		registry.cancel_unstarted(reused);
		registry.accepted(b);
		registry.accepted(b);
		registry.completed(b);
		rejects([&] { registry.settle(b, {}); });
		registry.completed(b);
		registry.settle(b, {}); // Explicit proved absence; never destructor-based refund.
		check(registry.stats().used == l::DiskUsage{} && registry.stats().reserved == l::DiskUsage{});
		const auto maximum = std::numeric_limits<std::uint64_t>::max();
		l::DiskLedger debt({1, 1, 1});
		debt.initialize({maximum, maximum, maximum});
		check(debt.stats().used == l::DiskUsage{maximum, maximum, maximum});
		rejects([&] { debt.reserve({1, 1, 1}); });
		debt.reclaimed({maximum, maximum, maximum});
		auto after_debt = debt.reserve({1, 1, 1});
		debt.cancel_unstarted(after_debt);
		rejects([&] { l::DiskLedger::generation_peak(maximum); });
		rejects([&] { l::DiskLedger::generation_peak(191); });
		check(l::DiskLedger::generation_peak(maximum - 304).bytes == maximum);
		l::DiskLedger uncertain({100, 100, 100});
		uncertain.initialize({});
		auto started = uncertain.reserve({10, 1, 1});
		uncertain.accepted(started);
		uncertain.fence();
		uncertain.completed(started); // Authentic originals still settle after fencing.
		check(uncertain.stats().reserved == l::DiskUsage{10, 1, 1});
		rejects([&] { uncertain.reserve({1, 1, 1}); });
		rejects([&] { uncertain.settle(started, {}); });
		rejects([&] { uncertain.reclaimed({}); });
		l::DiskLedger excess({10, 1, 1});
		excess.initialize({});
		auto too_small = excess.reserve({1, 1, 1});
		excess.accepted(too_small);
		excess.completed(too_small);
		rejects([&] { excess.settle(too_small, {2, 1, 1}); });
		check(excess.stats().fenced && excess.stats().reserved == l::DiskUsage{1, 1, 1});
		l::DiskLedger allocation_free({1000, 1000, 1000});
		watching = true;
		allocation_free.initialize({});
		for (unsigned turn = 0; turn < 100000; ++turn) {
			auto permit = allocation_free.reserve({1, 1, 1});
			allocation_free.accepted(permit);
			allocation_free.completed(permit);
			allocation_free.settle(permit, {1, 1, 1});
			allocation_free.reclaimed({1, 1, 1});
		}
		watching = false;
		check(allocations == 0 && allocation_free.stats().used == l::DiskUsage{});
		std::cout << "three-dimensional admission, peak overlap, retained debt and 100000 allocation-free "
					 "cycles passed\n";
	} catch (const std::exception &error) {
		watching = false;
		std::cerr << error.what() << '\n';
		return 1;
	}
}
