// Managed recovery compares each segment checksum and page digest once. These tests flip every byte
// of a stored pack and require the rejection (or acceptance) a recovery has always given, and count
// the bytes hashed.
#include "detent_local/reader.hh"
#include "paged_disk_fixture.hh"
#include "recovery_context.hh"
#include <fcntl.h>
#include <map>
#include <string>

namespace {
struct Outcome {
	std::string error;
	std::uint64_t records = 0, segment_hash_bytes = 0, page_calls = 0, pack_calls = 0;
};
Outcome attempt(const std::filesystem::path &directory) {
	Outcome outcome;
	l::paged::reset_hash_counts();
	auto heap = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
	auto mappings = std::make_shared<d::StorageLedger>(1024 * 1024);
	auto metrics = std::make_shared<l::detail::RecoveryMetrics>();
	{
		d::RetirementRuntime runtime(heap, 1);
		auto domain = runtime.domain(heap, 16384);
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = mappings;
		l::detail::RecoveryContext context{domain.factory(), heap, mappings, limits, {}, {}};
		context.metrics = metrics;
		try {
			auto recovered = l::detail::RecoveryEngine::recover(directory, context);
			outcome.records = recovered.view().size();
		} catch (const std::exception &error) {
			outcome.error = error.what();
		}
		domain.close().wait();
	}
	outcome.segment_hash_bytes = metrics->segment_hash_bytes;
	const auto hashes = l::paged::hash_counts();
	outcome.page_calls = hashes.page_calls;
	outcome.pack_calls = hashes.pack_calls;
	return outcome;
}
void flip(const std::filesystem::path &path, std::size_t at) {
	const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
	require(fd >= 0);
	char byte = 0;
	require(::pread(fd, &byte, 1, at) == 1);
	byte = static_cast<char>(byte ^ 0x01);
	require(::pwrite(fd, &byte, 1, at) == 1);
	require(::close(fd) == 0);
}
std::string slurp(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), {}};
}
} // namespace
int main() {
	const auto directory = std::filesystem::current_path() / ".scratch" /
	                       ("paged-hash-once-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder snapshot(source, generation);
	while (!snapshot.done()) {
		l::ValidationBudget budget;
		snapshot.step(budget);
	}
	auto image = snapshot.take();
	install(directory, image, storage);
	std::uint64_t pack_bytes = 0;
	for (const auto &segment : image.segments())
		pack_bytes += segment.bytes->size();

	// A clean recovery hashes every pack byte once.
	const auto clean = attempt(directory);
	require(clean.error.empty() && clean.records == image.root().records);
	require(clean.segment_hash_bytes == pack_bytes);
	require(clean.page_calls == image.root().pages && clean.pack_calls == image.root().segments);

	// A flipped pack byte is rejected where a recovery has always rejected it: the segment
	// checksum.
	std::map<std::string, std::size_t> plain, forged;
	std::size_t accepted = 0;
	for (const auto &segment : image.segments()) {
		const l::detail::SegmentNames names(segment.ownership.segment);
		const auto pack = directory / names.payload_name();
		const auto owner = directory / names.owner_name();
		const auto original = slurp(owner);
		for (std::size_t at = 0; at < segment.bytes->size(); ++at) {
			flip(pack, at);
			const auto outcome = attempt(directory);
			require(!outcome.error.empty());
			++plain[outcome.error];

			// With the owner record forged to match, only the page digests can reject.
			auto record = segment.ownership;
			const auto bytes = slurp(pack);
			const auto digest = l::paged::pack_commitment({bytes.data(), bytes.size()});
			std::copy(digest.begin(), digest.end(), record.checksum.begin());
			write(owner, l::encode_segment_ownership(record));
			const auto second = attempt(directory);
			if (second.error.empty()) {
				++accepted;
				require(second.records == image.root().records);
			} else
				++forged[second.error];
			write(owner, {original.data(), original.size()});
			flip(pack, at);
		}
	}
	require(attempt(directory).error.empty());
	require(image.root().pages == 1 && image.root().bytes == pack_bytes);
	require(plain.size() == 1 && plain.begin()->second == pack_bytes);
	std::uint64_t rejected = 0;
	for (const auto &[error, count] : forged)
		rejected += count;
	require(!accepted && rejected == pack_bytes);
}
