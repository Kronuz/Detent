#include "local_corpus.hh"
#include "paged_graph_validator.hh"
#include "paged_snapshot_builder.hh"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sys/resource.h>
namespace d = detent::distribution;
namespace l = detent::local;
using Clock = std::chrono::steady_clock;
struct Lease {
	l::detail::PagedImage image;
	std::vector<l::detail::SegmentInput, d::StorageAllocator<l::detail::SegmentInput>> segments;
	Lease(l::detail::PagedImage value, std::shared_ptr<d::StorageLedger> account)
		: image(std::move(value)), segments(d::StorageAllocator<l::detail::SegmentInput>(account)) {
		segments.reserve(image.segments().size());
		for (const auto &s : image.segments())
			segments.push_back({s.ownership, {s.bytes->data(), s.bytes->size()}});
		std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
		});
	}
};
int main(int argc, char **argv) {
	try {
		auto count = argc > 1 ? std::stoull(argv[1]) : 500000;
		if (!count || count > 500000 || argc > 2)
			throw std::invalid_argument("expected count 1..500000");
		auto corpus = detent::bench::local_corpus(count);
		d::SnapshotDecoder decoder(corpus.cursor, {"app"});
		auto input = std::span<const char>(corpus.wire);
		while (!input.empty()) {
			d::WorkBudget b;
			decoder.feed(input, b);
		}
		auto source = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
		auto account = source->storage_account();
		auto source_bytes = account->used();
		l::StorageIdentity storage{};
		storage.fill('s');
		l::GenerationId generation{};
		generation.fill('a');
		auto start = Clock::now();
		auto cpu = std::clock();
		l::detail::PagedSnapshotBuilder builder(source, storage, generation);
		while (!builder.done()) {
			l::ValidationBudget b;
			builder.step(b);
		}
		auto image = builder.take();
		auto built = Clock::now();
		auto build_cpu = std::clock() - cpu;
		std::uint64_t physical = image.bytes()->size();
		for (const auto &s : image.segments())
			physical += s.bytes->size();
		auto root = image.root();
		auto segment_count = image.segments().size();
		auto lease =
			std::allocate_shared<Lease>(d::StorageAllocator<Lease>(account), std::move(image), account);
		auto controls = std::make_shared<d::StorageLedger>(4 * l::paged::segment_bytes);
		d::RetirementRuntime runtime{controls, 1};
		auto domain = runtime.domain(controls, 16);
		cpu = std::clock();
		l::detail::GraphValidator validator(account, lease, lease->segments, root, storage);
		while (!validator.done()) {
			l::ValidationBudget b;
			validator.step(b);
		}
		auto view = validator.take(domain);
		auto validated = Clock::now();
		auto validation_cpu = std::clock() - cpu;
		std::vector<std::pair<std::string, std::uint32_t>> queries;
		const auto key_width = detent::bench::local_key_width(count);
		for (unsigned i = 0; i < 100000; ++i) {
			auto index = (std::uint64_t(i) * 7919 % corpus.integers) * 4;
			queries.emplace_back(detent::bench::local_key(index, key_width), index);
		}
		auto begin = Clock::now();
		for (const auto &[key, index] : queries) {
			auto result = view.get("app", key);
			if (!result || result.value() != -std::int64_t(index))
				throw std::runtime_error("lookup oracle failed");
		}
		auto us = [](auto elapsed) {
			return std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
		};
		rusage usage{};
		getrusage(RUSAGE_SELF, &usage);
		auto rss = std::uint64_t(usage.ru_maxrss);
#ifndef __APPLE__
		rss *= 1024;
#endif
		std::cout << "{\"records\":" << count << ",\"source_charged_bytes\":" << source_bytes
				  << ",\"physical_bytes\":" << physical << ",\"pages\":" << root.pages
				  << ",\"segments\":" << segment_count << ",\"peak_charged_bytes\":" << account->peak()
				  << ",\"peak_rss_bytes\":" << rss << ",\"build_us\":" << us(built - start)
				  << ",\"build_cpu_us\":" << std::uint64_t(build_cpu) * 1000000 / CLOCKS_PER_SEC
				  << ",\"validation_us\":" << us(validated - built)
				  << ",\"validation_cpu_us\":" << std::uint64_t(validation_cpu) * 1000000 / CLOCKS_PER_SEC
				  << ",\"lookup_100k_us\":" << us(Clock::now() - begin) << "}\n";
		view = {};
		lease.reset();
		domain.close().wait();
		l::ValidationBudget dispose;
		while (!builder.discard(dispose))
			dispose = {};
		if (account->used() != source_bytes)
			throw std::runtime_error("storage charge not conserved");
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
