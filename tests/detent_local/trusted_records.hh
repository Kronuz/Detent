#pragma once
// Test support: a hand-installed store has no `.read` or `.live`, which the agent's cold start requires.
// derive_trusted_records verifies the store with the full recovery once and writes both records from
// what that recovery derived, the way a publication does.
#include "detent_distribution/retirement.hh"
#include "detent_local/generation.hh"
#include "leased_paged_root.hh"
#include "live_record.hh"
#include "names.hh"
#include "prepared_reader.hh"
#include "recovery_context.hh"
#include "reader_metadata.hh"
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <unistd.h>

namespace detent::local::detail {
struct RecoveredTesting {
	static const LeasedPagedRoot &root(const RecoveredGeneration &g) { return *g.paged_; }
};
} // namespace detent::local::detail

namespace trusted_records {
inline void put(const std::filesystem::path &path, std::span<const char> bytes) {
	namespace x = detent::local::detail;
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		throw std::runtime_error("cannot write " + path.string());
	std::size_t at = 0;
	while (at < bytes.size()) {
		const auto n = ::write(fd, bytes.data() + at, bytes.size() - at);
		if (n <= 0)
			throw std::runtime_error("short write " + path.string());
		at += std::size_t(n);
	}
	::close(fd);
}
inline void derive(const std::filesystem::path &directory) {
	namespace d = detent::distribution;
	namespace l = detent::local;
	namespace x = l::detail;
	auto heap = std::make_shared<d::StorageLedger>(256ull << 20);
	auto mappings = std::make_shared<d::StorageLedger>(1ull << 30);
	d::RetirementRuntime runtime(heap, 1);
	auto domain = runtime.domain(heap, 16384);
	{
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = mappings;
		x::RecoveryContext context{domain.factory(), heap, mappings, limits, {}, {}};
		auto generation = x::RecoveryEngine::recover(directory, context);
		const auto &root = x::RecoveredTesting::root(generation);
		const auto &registry = *root.registry();
		const auto metadata = x::encode_reader_metadata(x::PreparedReader::describe(generation));
		x::LiveRecord live;
		live.generation = generation.descriptor().generation;
		live.root_checksum = generation.descriptor().checksum;
		std::copy(metadata.end() - x::live_digest_bytes, metadata.end(), live.read_digest.begin());
		std::copy(registry.override_histogram().begin(), registry.override_histogram().end(),
				  live.histogram.begin());
		for (std::size_t i = 0; i < registry.inputs().size(); ++i) {
			const auto &ownership = registry.inputs()[i].ownership;
			live.rows.push_back({ownership.segment, ownership.length, ownership.checksum, registry.reachability(i)});
		}
		std::vector<char> bytes(x::live_record_size(live.rows.size()));
		x::encode_live_record(live, bytes);
		const auto id = generation.descriptor().generation;
		put(directory / std::string(x::GenerationName(id, x::GenerationFile::ReaderMetadata).view()), metadata);
		put(directory / std::string(x::GenerationName(id, x::GenerationFile::Live).view()), bytes);
	}
	domain.close().wait();
}
} // namespace trusted_records
