#include "applied_delta_fixture.hh"
#include "detent_local/disk_store.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include <completion/native_completion.h>
namespace c = kronuz::io::completion;
template <class Error, class Call> void rejects(Call call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid paged disk publication accepted");
}
l::DiskUsage census(const std::filesystem::path &directory) {
	l::DiskUsage usage;
	for (const auto &entry : std::filesystem::directory_iterator(directory)) {
		usage.bytes += entry.file_size();
		++usage.entries;
		if (entry.path().extension() == ".lease")
			++usage.generations;
	}
	return usage;
}
void drive(const std::shared_ptr<l::DiskStore> &store, bool cancel_create = false) {
	unsigned turns = 0;
	while (auto job = store->operation()) {
		require(++turns < 100000);
		const auto kind = job->request().kind;
		job->submitted();
		if (kind == c::Kind::Replace)
			require(store->selection().knowledge == l::SelectionKnowledge::Uncertain);
		if (cancel_create && kind == c::Kind::CreateInto) {
			store->cancel_publication();
			cancel_create = false;
		}
		require(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
		store->advance();
	}
	store->advance();
}
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-disk-store-" + std::to_string(::getpid()));
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
	l::detail::PagedSnapshotBuilder construction(source, storage, generation);
	while (!construction.done()) {
		l::ValidationBudget budget;
		construction.step(budget);
	}
	auto image = construction.take();
	for (bool progress : {false, true}) {
		for (unsigned scenario = 0; scenario < 6; ++scenario) {
			const auto root = directory / (std::to_string(progress) + "-" + std::to_string(scenario));
			install(root, image, storage);
			write(root / "owner.lock", {});
			if (scenario == 5)
				write(root / (l::generation_name(next) + ".image"), {});
			auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
			auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
			d::RetirementRuntime runtime(baseline, 1);
			auto domain = runtime.domain(account, 64);
			const auto initial = account->used();
			l::DiskStoreOptions options;
			options.limits = {64 * l::paged::segment_bytes, 100, 10};
			options.recovery_limits.retirement_domain = &domain;
			auto mapping_account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
			options.recovery_limits.mapping_account = mapping_account;
			auto base =
				std::optional<l::RecoveredGeneration>(l::recover_generation(root, options.recovery_limits));
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
				l::detail::PagedDeltaPlanBuilder builder(*base, parse_delta(false, 2, "app", account), next,
														 domain);
				while (!builder.done()) {
					l::ValidationBudget budget;
					builder.step(budget);
				}
				plan.emplace(builder.take());
			}
			const auto wanted = plan->descriptor();
			const auto logical = plan->root().logical;
			std::vector<std::uint64_t> lengths;
			for (const auto &segment : plan->segments())
				lengths.push_back(segment.bytes->size());
			const auto peak = l::DiskLedger::paged_peak(lengths);
			if (scenario == 3)
				options.limits.bytes = census(root).bytes + peak.bytes - 1;
			auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
			drive(store);
			require(store->status() == l::StoreStatus::Ready);
			if (scenario == 3) {
				rejects<l::DiskPressure>([&] { store->begin(std::move(*plan)); });
				require(plan->descriptor() == wanted && store->publication_ready());
			} else {
				if (scenario == 4 && progress) {
					const auto &segment = image.segments().front();
					auto ownership = segment.ownership;
					ownership.checksum[0] ^= 1;
					write(root / l::detail::SegmentNames(ownership.segment).owner_name(),
						  l::encode_segment_ownership(ownership));
				}
				store->begin(std::move(*plan));
				rejects<std::logic_error>([&] { plan->root(); });
				if (scenario == 1)
					store->cancel_publication();
				drive(store, scenario == 2);
				if (scenario == 1 || scenario == 2) {
					require(store->publication_status() == l::DiskPublicationStatus::Cancelled);
					require(store->selection().descriptor == image.descriptor());
				} else if (scenario == 4 && progress) {
					require(store->publication_status() == l::DiskPublicationStatus::Fenced);
				} else if (scenario == 5) {
					require(store->publication_status() == l::DiskPublicationStatus::Rejected);
					require(store->selection().descriptor == image.descriptor());
				} else {
					require(store->publication_status() == l::DiskPublicationStatus::Published);
					require(store->selection().descriptor == wanted && !store->publication_ready());
					rejects<std::invalid_argument>([&] { store->confirm_recovered(*base); });
					auto recovered =
						l::recover_generation(root, options.recovery_limits, wanted.cursor, &*base);
					store->confirm_recovered(recovered);
					require(store->publication_ready());
					require(recovered.view().get("app", "count").value() == (progress ? INT64_MIN : 1));
					if (!progress) {
						l::detail::PagedSnapshotBuilder oracle(expected(false, 2), storage, next);
						while (!oracle.done()) {
							l::ValidationBudget budget;
							oracle.step(budget);
						}
						require(oracle.take().root().logical == logical);
					} else
						require(logical == image.root().logical);
				}
			}
			if (store->status() == l::StoreStatus::Ready)
				require(store->stats().used == census(root) && store->stats().reserved == l::DiskUsage{});
			store->close();
			drive(store);
			require(store->drained());
			store.reset();
			plan.reset();
			base.reset();
			domain.close().wait();
			require(account->used() == initial && mapping_account->used() == 0);
		}
	}
}
