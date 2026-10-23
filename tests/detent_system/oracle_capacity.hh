#pragma once
#include "detent_local/view.hh"
#include <array>
#include <chrono>
#include <sys/resource.h>
#include <charconv>
#include <limits>
#include <stdexcept>

struct OracleCapacity {
	detent::local::ViewLimits limits;
	std::shared_ptr<detent::distribution::StorageLedger> heap;
	std::unique_ptr<detent::distribution::RetirementRuntime> runtime;
	std::optional<detent::distribution::RetirementDomain> domain;

	OracleCapacity(int argc, char** argv) {
		if (argc == 7)
			return;
		if (argc != 14)
			throw std::invalid_argument("oracle requires seven optional capacity integers");
		std::array<std::uint64_t, 7> values{};
		for (unsigned index = 0; index != values.size(); ++index) {
			const std::string_view text(argv[index + 7]);
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), values[index]);
			if (error != std::errc{} || end != text.data() + text.size())
				throw std::invalid_argument("invalid oracle capacity integer");
		}
		if (!values[0] || values[0] > UINT32_MAX || !values[5] || !values[6] ||
			values[5] > SIZE_MAX || values[6] > SIZE_MAX)
			throw std::invalid_argument("invalid oracle capacity envelope");
		limits.records = values[0];
		if (values[1]) limits.bytes = values[1];
		if (values[2]) limits.strings = values[2];
		if (values[3]) limits.pages = values[3];
		if (values[4]) limits.segments = values[4];
		heap = std::make_shared<detent::distribution::StorageLedger>(values[5]);
		runtime = std::make_unique<detent::distribution::RetirementRuntime>(heap, 2);
		domain.emplace(runtime->domain(heap, 4096));
		limits.retirement_domain = &*domain;
		limits.mapping_account = std::make_shared<detent::distribution::StorageLedger>(values[6]);
	}
};

inline std::uint64_t oracle_peak_rss_bytes() {
	rusage usage{};
	if (::getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0)
		throw std::runtime_error("oracle resource usage unavailable");
	const auto bytes = static_cast<std::uint64_t>(usage.ru_maxrss);
#ifdef __APPLE__
	return bytes;
#else
	if (bytes > UINT64_MAX / 1024)
		throw std::overflow_error("oracle RSS overflow");
	return bytes * 1024;
#endif
}
