#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_compaction_plan.hh"
#include "paged_disk_fixture.hh"
#include "paged_split_fixture.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <fcntl.h>
#include <source_location>
#include <sys/file.h>
#include <thread>
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("compaction invariant at " + std::to_string(location.line()));
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	for (unsigned turns = 0; store->operation() || store->needs_work(); ++turns) {
		check(turns < 1000000);
		if (auto operation = store->operation()) {
			const auto &request = operation->request();
			operation->submitted();
			check(operation->complete(kronuz::journal::detail::execute_primitive(operation->io(), request)));
		}
		store->advance();
	}
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
void run(std::size_t choice) {
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("paged-compaction-" + std::to_string(::getpid()) + "-" + std::to_string(choice));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{directory};
	auto heap = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
	auto mappings = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
	auto controls = std::make_shared<d::StorageLedger>(1024 * 1024);
	d::RetirementRuntime runtime(controls, 1);
	auto domain = runtime.domain(heap, 512);
	{
		auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
		l::StorageIdentity storage{};
		storage.fill('s');
		l::GenerationId generation{}, split_generation{}, next{};
		generation.fill('g');
		split_generation.fill('x');
		next.fill('n');
		l::detail::PagedSnapshotBuilder builder(source, storage, generation);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		auto image = builder.take();
		std::vector<l::detail::SegmentInput> inputs;
		for (const auto &segment : image.segments())
			inputs.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
		auto split = std::make_shared<l::testing::SplitPagedInput>(heap, inputs, image.root(), storage,
																   split_generation);
		const auto encoded = l::paged::encode_root(split->root);
		l::Descriptor descriptor{generation, encoded.size(), l::paged::physical_commitment(encoded),
								 split->root.cursor, split->root.profile};
		std::filesystem::create_directories(directory);
		check(::chmod(directory.c_str(), 0700) == 0);
		write(directory / "storage.meta", l::encode_storage_identity(storage));
		write(directory / "owner.lock", {});
		const auto name = l::generation_name(generation);
		write(directory / (name + ".image"), encoded);
		write(
			directory / (name + ".lease"),
			l::encode_generation_ownership({storage, generation, descriptor.length, descriptor.checksum, 2}));
		write(directory / (name + ".descriptor"), l::encode_descriptor(descriptor));
		write(directory / "selected", l::encode_descriptor(descriptor));
		for (const auto &segment : split->packs) {
			const l::detail::SegmentNames names(segment.ownership.segment);
			write(directory / names.payload_name(), *segment.bytes);
			write(directory / names.owner_name(), l::encode_segment_ownership(segment.ownership));
		}
		l::DiskStoreOptions options;
		options.limits = {64 * l::paged::segment_bytes, 1000, 10};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = mappings;
		std::optional<l::RecoveredGeneration> base(l::recover_generation(directory, options.recovery_limits));
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		if (store->error())
			std::rethrow_exception(store->error());
		const std::array selected{split->packs[0].ownership.segment, split->packs[1].ownership.segment};
		const auto selection = choice == 2 ? std::span<const l::StorageIdentity>(selected)
										   : std::span<const l::StorageIdentity>(&selected[choice], 1);
		for (const auto limits : {l::detail::CompactionLimits{8, 1, 16 * 1024 * 1024},
								  l::detail::CompactionLimits{8, 32768, 1}}) {
			bool refused = false;
			try {
				l::detail::PagedCompactionBuilder bounded(*base, selection, next, domain, limits);
				while (!bounded.done()) {
					l::ValidationBudget budget;
					bounded.step(budget);
				}
			} catch (const std::length_error &) {
				refused = true;
			}
			check(refused && store->selection().descriptor == descriptor);
			check(store->stats().reserved == l::DiskUsage{});
		}
		l::detail::PagedCompactionBuilder compaction(*base, selection, next, domain);
		unsigned turns = 0;
		while (!compaction.done()) {
			l::ValidationBudget budget{1, 1};
			compaction.step(budget);
			check(++turns < 1000000);
		}
		check(compaction.visited_pages() == split->root.pages && compaction.relocated_pages() > 0);
		auto plan = compaction.take();
		const auto wanted = plan.descriptor();
		check(plan.maintenance() && wanted.cursor == descriptor.cursor &&
			  plan.root().logical == split->root.logical);
		{
			const auto before = store->stats();
			auto pressure = mappings->admit(mappings->limit() - mappings->used());
			bool refused = false;
			try {
				store->begin(std::move(plan));
			} catch (const d::StoragePressure &) {
				refused = true;
			}
			check(refused && plan.descriptor() == wanted && store->publication_ready());
			check(store->stats().used == before.used && store->stats().reserved == l::DiskUsage{});
			check(l::read_generation_selection(directory).descriptor == descriptor);
			check(!std::filesystem::exists(directory / (l::generation_name(next) + ".image")));
		}
		l::GenerationId stale_id{};
		stale_id.fill('q');
		l::detail::PagedCompactionBuilder stale_builder(*base, selection, stale_id, domain);
		while (!stale_builder.done()) {
			l::ValidationBudget budget;
			stale_builder.step(budget);
		}
		std::optional<l::detail::PagedPlan> stale(stale_builder.take());
		store->begin(std::move(plan));
		drive(store);
		check(store->publication_status() == l::DiskPublicationStatus::Published);
		auto recovered =
			l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store));
		store->confirm_recovered(recovered);
		bool refused = false;
		const auto stale_descriptor = stale->descriptor();
		try {
			store->begin(std::move(*stale));
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		check(refused && stale->descriptor() == stale_descriptor && store->selection().descriptor == wanted);
		check(store->stats().reserved == l::DiskUsage{});
		check(!std::filesystem::exists(directory / (l::generation_name(stale_id) + ".image")));
		stale.reset();
		check(recovered.view().get("app", "count").value() == INT64_MIN &&
			  base->view().get("app", "count").value() == INT64_MIN);
		const l::detail::SegmentNames evacuated(selection.front());
		store->request_collection();
		store->advance();
		drive(store);
		check(std::filesystem::exists(directory / evacuated.payload_name()));
		base.reset();
		const auto lease = ::open((directory / (name + ".lease")).c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
		check(lease >= 0);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (::flock(lease, LOCK_EX | LOCK_NB) != 0) {
			check(std::chrono::steady_clock::now() < deadline);
			std::this_thread::yield();
		}
		check(::flock(lease, LOCK_UN) == 0 && ::close(lease) == 0);
		store->request_collection();
		store->advance();
		drive(store);
		for (const auto &identity : selection) {
			const l::detail::SegmentNames names(identity);
			check(!std::filesystem::exists(directory / names.payload_name()) &&
				  !std::filesystem::exists(directory / names.owner_name()));
		}
		check(store->stats().used == census(directory) && store->stats().reserved == l::DiskUsage{});
		check(recovered.view().eval("app", "flag", "host-a", "beta").has_value());
		store->close();
		drive(store);
		check(store->drained());
	}
	domain.close().wait();
	check(mappings->used() == 0);
}
int main() {
	run(0);
	run(1);
	run(2);
}
