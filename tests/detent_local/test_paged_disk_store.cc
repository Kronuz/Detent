#include "applied_delta_fixture.hh"
#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include "prepared_reader.hh"
#include <chrono>
#include <source_location>
#include <thread>
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("paged disk invariant at line " + std::to_string(where.line()));
}
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
// The store's work runs here, as the coordinator's worker runs it; advance() settles it.
void drive(const std::shared_ptr<l::DiskStore> &store) {
	for (unsigned turns = 0;; ++turns) {
		check(turns < 100000);
		store->advance();
		auto work = store->work();
		if (!work)
			break;
		work->run();
	}
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
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture(1, 24 + 32 * 128, 25, 42, 32)));
	l::GenerationId generation{}, next{};
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder construction(source, generation);
	while (!construction.done()) {
		l::ValidationBudget budget;
		construction.step(budget);
	}
	auto image = construction.take();
	for (bool progress : {false, true}) {
		for (unsigned scenario = 0; scenario < 9; ++scenario) {
			if (scenario == 6 && progress)
				continue;
			const auto root = directory / (std::to_string(progress) + "-" + std::to_string(scenario));
			install(root, image);
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
				l::detail::PagedPlanBuilder builder(*base, delta, domain);
				while (!builder.done()) {
					l::ValidationBudget budget;
					builder.step(budget);
				}
				plan.emplace(builder.take());
			} else {
				l::detail::PagedDeltaPlanBuilder builder(*base, scenario == 6 ? one_key_delta(account) : parse_delta(false, 2, "app", account), domain);
				while (!builder.done()) {
					l::ValidationBudget budget;
					builder.step(budget);
				}
				plan.emplace(builder.take());
			}
			const auto wanted = plan->descriptor();
			const auto logical = l::paged::state_commitment(plan->root());
			std::vector<std::uint64_t> lengths;
			for (const auto &segment : plan->segments())
				lengths.push_back(segment.bytes->size());
			std::size_t reached = lengths.size();
			if (const auto base = plan->registry())
				for (std::size_t i = 0; i < base->inputs().size(); ++i)
					reached += plan->base_reachability(i).pages != 0;
			const auto peak = l::DiskLedger::paged_peak(lengths, l::detail::live_record_size(reached));
			if (scenario == 3)
				options.limits.bytes = census(root).bytes + peak.bytes - 1;
			if (scenario == 6) {
				std::uint64_t physical = l::paged::root_bytes;
				const auto registry = plan->registry();
				for (std::size_t i = 0; i < registry->inputs().size(); ++i)
					if (plan->base_reachability(i).pages)
						physical += registry->inputs()[i].ownership.length;
				for (auto length : lengths)
					physical += length;
				if (physical <= plan->root().bytes + l::paged::root_bytes)
					throw std::runtime_error(
						"physical-limit fixture needs retained pack: " + std::to_string(physical) + "/" +
						std::to_string(plan->root().bytes));
				options.recovery_limits.bytes = physical - 1;
			}
			// An orphan under the derived name, as a cut publication leaves it: unlinked and written again.
			if (scenario == 5)
				write(root / (l::generation_name(wanted.generation) + ".image"), {});
			auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
			drive(store);
			check(store->status() == l::StoreStatus::Ready);
			if (scenario == 3 || scenario == 6) {
				if (scenario == 3)
					rejects<l::DiskPressure>([&] { store->begin(std::move(*plan)); });
				else
					rejects<std::invalid_argument>([&] { store->begin(std::move(*plan)); });
				check(plan->descriptor() == wanted && store->publication_ready());
			} else {
				if (scenario == 4 && progress) {
					const auto &segment = image.segments().front();
					auto ownership = segment.ownership;
					ownership.checksum[0] ^= 1;
					write(root / l::detail::SegmentNames(ownership.segment).owner_name(),
						  l::encode_segment_ownership(ownership));
				}
				if (scenario == 7) {
					{
						auto pressure = account->admit(account->limit() - account->used());
						rejects<d::StoragePressure>([&] { store->begin(std::move(*plan)); });
					}
					check(plan->descriptor() == wanted && store->publication_ready());
					check(store->selection().descriptor == image.descriptor());
					check(store->stats().reserved == l::DiskUsage{});
				}
				if (scenario == 8) {
					const auto mapped = mapping_account->used();
					std::uint64_t needed = l::detail::rounded_mapping_bytes(wanted.length);
					for (const auto &segment : plan->segments())
						needed += l::detail::rounded_mapping_bytes(segment.ownership.length);
					{
						auto pressure =
							mapping_account->admit(mapping_account->limit() - mapped - needed + 1);
						rejects<d::StoragePressure>([&] { store->begin(std::move(*plan)); });
						check(plan->descriptor() == wanted && store->publication_ready());
						check(store->selection().descriptor == image.descriptor());
						check(store->stats().reserved == l::DiskUsage{});
						check(!std::filesystem::exists(root / (l::generation_name(wanted.generation) + ".image")));
						for (const auto &segment : plan->segments()) {
							const l::detail::SegmentNames names(segment.ownership.segment);
							check(!std::filesystem::exists(root / names.payload_name()));
							check(!std::filesystem::exists(root / names.owner_name()));
						}
					}
					const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
					while (mapping_account->used() != mapped) {
						check(std::chrono::steady_clock::now() < deadline);
						std::this_thread::yield();
					}
				}
				store->begin(std::move(*plan));
				rejects<std::logic_error>([&] { plan->root(); });
				if (scenario == 1)
					store->cancel_publication();
				if (scenario == 2) {
					// A cancellation after the work is taken changes nothing: the cycle runs to its end.
					auto work = store->work();
					store->cancel_publication();
					work->run();
				}
				drive(store);
				if (scenario == 1) {
					check(store->publication_status() == l::DiskPublicationStatus::Cancelled);
					check(store->selection().descriptor == image.descriptor());
				} else if (scenario == 4 && progress) {
					// The store reuses a reached segment without reading it; recovery refuses its damage.
					check(store->publication_status() == l::DiskPublicationStatus::Published);
					bool refused = false;
					try {
						l::detail::ManagedRecovery::recover(root, l::detail::ManagedRecovery::capture(*store));
					} catch (const std::exception &) {
						refused = true;
					}
					check(refused);
				} else {
					check(store->publication_status() == l::DiskPublicationStatus::Published);
					check(store->selection().descriptor == wanted && !store->publication_ready());
					rejects<std::invalid_argument>([&] { store->confirm_recovered(*base); });
					{
						const auto prepared =
						    l::detail::PreparedReader::open(root, ::getuid(), options.recovery_limits);
						check(prepared.descriptor == wanted && prepared.state_commitment == logical);
						check(prepared.view.get("app", "count").value() == (progress ? INT64_MIN : 1));
					}
					auto recovered = l::detail::ManagedRecovery::recover(
						root, l::detail::ManagedRecovery::capture(*store));
					{
						auto pressure = account->admit(account->limit() - account->used());
						store->confirm_recovered(recovered);
					}
					check(store->publication_ready());
					check(recovered.view().get("app", "count").value() == (progress ? INT64_MIN : 1));
					if (!progress) {
						l::detail::PagedSnapshotBuilder oracle(expected(false, 2), next);
						while (!oracle.done()) {
							l::ValidationBudget budget;
							oracle.step(budget);
						}
						check(l::paged::state_commitment(oracle.take().root()) == logical);
					} else
						check(logical == l::paged::state_commitment(image.root()));
				}
			}
			if (store->status() == l::StoreStatus::Ready)
				check(store->stats().used == census(root) && store->stats().reserved == l::DiskUsage{});
			store->close();
			drive(store);
			check(store->drained());
			store.reset();
			plan.reset();
			base.reset();
			domain.close().wait();
			check(account->used() == initial && mapping_account->used() == 0);
		}
	}
}
