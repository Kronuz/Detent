#include "applied_delta_fixture.hh"
#include "detent_local/disk_store.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include <completion/native_completion.h>
namespace c = kronuz::io::completion;
void drive(const std::shared_ptr<l::DiskStore> &store) {
	unsigned turns = 0;
	while (store->operation() || store->needs_work()) {
		auto job = store->operation();
		if (!job) {
			store->advance();
			continue;
		}
		require(++turns < 100000);
		job->submitted();
		require(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
		store->advance();
	}
	store->advance();
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
std::vector<c::Kind> run(const std::filesystem::path &root, const l::detail::PagedImage &image,
						 const l::StorageIdentity &storage, bool progress, std::size_t point, unsigned mode) {
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
	auto base = std::optional<l::RecoveredGeneration>(l::recover_generation(root, options.recovery_limits));
	l::GenerationId next;
	next.fill('n');
	std::optional<l::detail::PagedPlan> plan;
	if (progress) {
		auto delta = std::make_shared<const d::ValidatedDelta>(parse_delta(true, 2, "app", account));
		l::detail::PagedPlanBuilder builder(*base, delta, next, domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		plan.emplace(builder.take());
	} else {
		l::detail::PagedDeltaPlanBuilder builder(*base, parse_delta(false, 2, "app", account), next, domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		plan.emplace(builder.take());
	}
	const auto wanted = plan->descriptor();
	auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
	drive(store);
	require(store->publication_ready());
	const auto injected = std::make_exception_ptr(std::runtime_error("injected paged publication fault"));
	store->begin(std::move(*plan));
	std::vector<c::Kind> trace;
	bool fired = false;
	{
		auto job = store->operation();
		while (!job->done()) {
			require(trace.size() < 100000);
			const bool fault = trace.size() == point;
			if (fault && mode == 2) {
				store->cancel_publication();
				fired = true;
				if (job->done())
					break;
			}
			const auto &request = job->request();
			const auto kind = request.kind;
			trace.push_back(kind);
			job->submitted();
			if (fault && (mode == 3 || mode == 4)) {
				store->cancel_publication();
				fired = true;
			}
			c::Result result;
			result.token = request.token;
			if (!fault || (mode != 0 && mode != 4) || kind == c::Kind::CloseFile)
				result = kronuz::journal::detail::execute_primitive(job->io(), request);
			if (fault && (mode < 2 || mode == 4)) {
				require(!result.error);
				// Failed reusable destinations are closed even if creation left named debt.
				if (mode == 1 && (kind == c::Kind::CreateInto || kind == c::Kind::OpenInto))
					request.file->close();
				result.error = injected;
				fired = true;
			}
			require(job->complete(std::move(result)));
		}
	}
	store->advance();
	if (point != SIZE_MAX)
		require(fired);
	const auto actual = census(root);
	const auto stats = store->stats();
	if (point != SIZE_MAX && (mode < 2 || mode == 4)) {
		require(store->status() == l::StoreStatus::Fenced);
		require(store->publication_status() == l::DiskPublicationStatus::Fenced &&
				!store->publication_ready());
		require(stats.fenced && stats.used.bytes + stats.reserved.bytes >= actual.bytes &&
				stats.used.entries + stats.reserved.entries >= actual.entries);
	} else {
		require(store->publication_status() == l::DiskPublicationStatus::Published ||
				store->publication_status() == l::DiskPublicationStatus::Cancelled);
		require(stats.used == actual && stats.reserved == l::DiskUsage{});
	}
	{
		auto recovered = l::recover_generation(root, options.recovery_limits);
		require(recovered.descriptor() == image.descriptor() || recovered.descriptor() == wanted);
		if (store->publication_status() == l::DiskPublicationStatus::Published) {
			require(recovered.descriptor() == wanted && !store->publication_ready());
			store->confirm_recovered(recovered);
			require(store->publication_ready());
		}
	}
	store->close();
	drive(store);
	require(store->drained());
	store.reset();
	plan.reset();
	base.reset();
	domain.close().wait();
	require(account->used() == initial && options.recovery_limits.mapping_account->used() == 0);
	return trace;
}
int main() {
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("paged-publication-faults-" + std::to_string(::getpid()));
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
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	unsigned cases = 0;
	for (bool progress : {false, true}) {
		const auto reference =
			run(directory / std::to_string(cases++), image, storage, progress, SIZE_MAX, 0);
		for (unsigned mode = 0; mode < 4; ++mode)
			for (std::size_t point = 0; point < reference.size(); ++point)
				run(directory / std::to_string(cases++), image, storage, progress, point, mode);
		for (std::size_t point = 0; point < reference.size(); ++point)
			if (reference[point] == c::Kind::Read)
				run(directory / std::to_string(cases++), image, storage, progress, point, 4);
	}
	std::cout << cases << " paged publication original-boundary fault and cancellation cases passed\n";
}
