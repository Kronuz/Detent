#include "applied_delta_fixture.hh"
#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <fcntl.h>
#include <sys/file.h>
#include <thread>
namespace c = kronuz::io::completion;
l::DiskUsage census(const std::filesystem::path &directory) {
	l::DiskUsage usage;
	for (const auto &entry : std::filesystem::directory_iterator(directory)) {
		++usage.entries;
		if (entry.is_regular_file())
			usage.bytes += entry.file_size();
		if (entry.path().extension() == ".lease")
			++usage.generations;
	}
	return usage;
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	for (unsigned turns = 0; store->operation() || store->needs_work(); ++turns) {
		require(turns < 100000);
		auto operation = store->operation();
		if (!operation) {
			store->advance();
			continue;
		}
		operation->submitted();
		require(operation->complete(
			kronuz::journal::detail::execute_primitive(operation->io(), operation->request())));
		store->advance();
	}
}
int main() {
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("paged-collection-publication-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{}, next{};
	storage.fill('s');
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	install(directory, image, storage);
	write(directory / "owner.lock", {});
	l::StorageIdentity orphan{};
	orphan.fill('z');
	const l::detail::SegmentNames names(orphan);
	const std::string bytes(257, 'x');
	write(directory / names.owner_name(),
		  l::encode_segment_ownership({storage, orphan, bytes.size(), l::paged::physical_commitment(bytes)}));
	write(directory / names.payload_name(), bytes);
	auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 128);
	l::DiskStoreOptions options;
	options.limits = {64 * l::paged::segment_bytes, 100, 10};
	options.recovery_limits.retirement_domain = &domain;
	options.recovery_limits.mapping_account =
		std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	auto base =
		std::optional<l::RecoveredGeneration>(l::recover_generation(directory, options.recovery_limits));
	l::detail::PagedDeltaPlanBuilder sparse(*base, parse_delta(false, 2, "app", account), next, domain);
	while (!sparse.done()) {
		l::ValidationBudget budget;
		sparse.step(budget);
	}
	auto plan = sparse.take();
	const auto wanted = plan.descriptor();
	std::vector<l::StorageIdentity> new_segments;
	for (const auto &segment : plan.segments())
		new_segments.push_back(segment.ownership.segment);
	require(!new_segments.empty());
	for (std::size_t i = 0; i < plan.registry()->inputs().size(); ++i)
		require(plan.base_reachability(i).pages == 0);
	auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
	drive(store);
	store->request_collection();
	store->advance();
	bool seen_candidate = false, paused = false;
	for (unsigned turns = 0; store->operation() || store->needs_work(); ++turns) {
		require(turns < 100000);
		auto operation = store->operation();
		if (!operation) {
			store->advance();
			continue;
		}
		const auto request = operation->request();
		if (request.kind == c::Kind::OpenCandidateInto && request.source.starts_with("segment-"))
			seen_candidate = true;
		operation->submitted();
		require(operation->complete(kronuz::journal::detail::execute_primitive(operation->io(), request)));
		if (seen_candidate && !store->operation() && !store->needs_work() && store->publication_ready()) {
			paused = true;
			break;
		}
		store->advance();
	}
	require(paused);
	store->begin(std::move(plan));
	drive(store);
	require(store->publication_status() == l::DiskPublicationStatus::Published);
	auto target = std::optional<l::RecoveredGeneration>(
		l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store)));
	store->confirm_recovered(*target);
	drive(store);
	require(!store->collection_error() && !store->stats().fenced && store->stats().used == census(directory));
	require(target->descriptor() == wanted && target->view().get("app", "count").value() == 1 &&
			base->view().get("app", "count").value() == INT64_MIN);
	for (const auto &id : new_segments) {
		const l::detail::SegmentNames protected_names(id);
		require(std::filesystem::exists(directory / protected_names.payload_name()));
	}
	for (const auto &segment : image.segments()) {
		const l::detail::SegmentNames protected_names(segment.ownership.segment);
		require(std::filesystem::exists(directory / protected_names.payload_name()));
	}
	require(!std::filesystem::exists(directory / names.payload_name()));
	base.reset();
	const auto lease_path = directory / (l::generation_name(generation) + ".lease");
	const int lease = ::open(lease_path.c_str(), O_RDWR | O_CLOEXEC);
	require(lease >= 0);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (::flock(lease, LOCK_EX | LOCK_NB) != 0) {
		require(std::chrono::steady_clock::now() < deadline);
		std::this_thread::yield();
	}
	require(::close(lease) == 0);
	store->request_collection();
	store->advance();
	drive(store);
	require(!store->collection_error() && store->stats().used == census(directory));
	for (const auto &segment : image.segments()) {
		const l::detail::SegmentNames old_names(segment.ownership.segment);
		require(!std::filesystem::exists(directory / old_names.payload_name()));
	}
	target.reset();
	store->close();
	drive(store);
	require(store->drained());
	store.reset();
	domain.close().wait();
	require(options.recovery_limits.mapping_account->used() == 0);
}
