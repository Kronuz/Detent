#include "applied_delta_fixture.hh"
#include "detent_local/disk_store.hh"
#include "paged_delta_plan.hh"
#include "paged_progress_fixture.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <fcntl.h>
#include <source_location>
#include <sys/file.h>
#include <thread>
namespace c = kronuz::io::completion;
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("paged root reclaim invariant at line " + std::to_string(where.line()));
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	unsigned turns = 0;
	while (store->operation() || store->needs_work()) {
		auto job = store->operation();
		if (!job) {
			store->advance();
			continue;
		}
		check(++turns < 100000);
		job->submitted();
		check(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
		store->advance();
	}
	store->advance();
}
void unpinned(const std::filesystem::path &path) {
	const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
	check(fd >= 0);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (::flock(fd, LOCK_EX | LOCK_NB)) {
		check(errno == EWOULDBLOCK || errno == EAGAIN);
		check(std::chrono::steady_clock::now() < deadline);
		std::this_thread::yield();
	}
	check(::flock(fd, LOCK_UN) == 0);
	::close(fd);
}
l::DiskUsage census(const std::filesystem::path &root) {
	l::DiskUsage usage;
	for (const auto &entry : std::filesystem::directory_iterator(root)) {
		usage.bytes += entry.file_size();
		++usage.entries;
		if (entry.path().extension() == ".lease")
			++usage.generations;
	}
	return usage;
}
int main() {
	const auto root =
		std::filesystem::current_path() / ".scratch" / ("paged-root-reclaim-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{root};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{}, next{}, last{};
	storage.fill('s');
	generation.fill('g');
	next.fill('n');
	last.fill('p');
	l::detail::PagedSnapshotBuilder snapshot(source, storage, generation);
	while (!snapshot.done()) {
		l::ValidationBudget budget;
		snapshot.step(budget);
	}
	auto image = snapshot.take();
	install(root, image, storage);
	write(root / "owner.lock", {});
	auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 64);
	const auto initial = account->used();
	l::DiskStoreOptions options;
	options.limits = {64 * l::paged::segment_bytes, 100, 10};
	options.recovery_limits.retirement_domain = &domain;
	options.recovery_limits.mapping_account =
		std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
	drive(store);
	auto base = std::optional<l::RecoveredGeneration>(l::recover_generation(root, options.recovery_limits));
	{
		l::detail::PagedDeltaPlanBuilder builder(*base, parse_delta(false, 2, "app", account), next, domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		store->begin(builder.take());
		drive(store);
	}
	auto pinned = std::optional<l::RecoveredGeneration>(l::recover_generation(root, options.recovery_limits));
	store->confirm_recovered(*pinned);
	base.reset();
	{
		l::detail::PagedPlanBuilder builder(*pinned, progress(pinned->descriptor().cursor, account), last,
											domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		store->begin(builder.take());
		drive(store);
	}
	{
		auto current = l::recover_generation(root, options.recovery_limits, {}, &*pinned);
		store->confirm_recovered(current);
	}
	unpinned(root / (l::generation_name(generation) + ".lease"));
	store->request_collection();
	store->advance();
	drive(store);
	check(!std::filesystem::exists(root / (l::generation_name(generation) + ".lease")));
	check(std::filesystem::exists(root / (l::generation_name(next) + ".lease")));
	check(pinned->view().get("app", "count").value() == 1);
	check(store->stats().used == census(root) && store->stats().used.generations == 2);
	pinned.reset();
	unpinned(root / (l::generation_name(next) + ".lease"));
	store->request_collection();
	store->advance();
	drive(store);
	check(!std::filesystem::exists(root / (l::generation_name(next) + ".lease")));
	check(std::filesystem::exists(root / (l::generation_name(last) + ".lease")));
	check(store->stats().used == census(root) && store->stats().used.generations == 1);
	{
		auto current = l::recover_generation(root, options.recovery_limits);
		check(current.view().get("app", "count").value() == 1);
	}
	{
		auto reader =
			std::optional<l::RecoveredGeneration>(l::recover_generation(root, options.recovery_limits));
		auto target = reader->descriptor().cursor;
		++target.revision;
		const auto wire = fixture(1, 24, 25, target.revision);
		d::Limits decode_limits;
		decode_limits.storage_account = account;
		d::SnapshotDecoder decoder(target, {"app"}, decode_limits);
		auto input = std::span<const char>(wire);
		while (!input.empty()) {
			d::WorkBudget budget{1, 1};
			decoder.feed(input, budget);
		}
		auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
		l::GenerationId legacy;
		legacy.fill('v');
		{
			l::ImageBuilder builder(snapshot, legacy, options.recovery_limits);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			{
				auto pressure = account->admit(account->limit() - account->used());
				bool refused = false;
				try {
					store->begin(builder, reader->descriptor().cursor);
				} catch (const d::StoragePressure &) {
					refused = true;
				}
				check(refused && store->publication_ready());
				check(store->stats().reserved == l::DiskUsage{});
			}
			store->begin(builder, reader->descriptor().cursor);
			drive(store);
		}
		check(store->publication_status() == l::DiskPublicationStatus::Published);
		store->request_collection();
		store->advance();
		drive(store);
		check(std::filesystem::exists(root / (l::generation_name(last) + ".lease")));
		check(reader->view().get("app", "count").value() == 1);
		reader.reset();
		unpinned(root / (l::generation_name(last) + ".lease"));
		store->request_collection();
		store->advance();
		drive(store);
		check(!std::filesystem::exists(root / (l::generation_name(last) + ".lease")));
		check(store->stats().used == census(root) && store->stats().used.generations == 1);
	}
	store->close();
	drive(store);
	check(store->drained());
	store.reset();
	domain.close().wait();
	check(account->used() == initial && options.recovery_limits.mapping_account->used() == 0);
}
