#include "detent_local/disk_store.hh"
#include "paged_disk_fixture.hh"
#include <completion/native_completion.h>
namespace c = kronuz::io::completion;
unsigned drive(const std::shared_ptr<l::DiskStore> &store) {
	unsigned syncs = 0, turns = 0;
	while (store->operation() || store->needs_work()) {
		auto job = store->operation();
		if (!job) {
			store->advance();
			continue;
		}
		require(++turns < 100000);
		if (job->request().kind == c::Kind::Sync)
			++syncs;
		job->submitted();
		require(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
		store->advance();
	}
	return syncs;
}
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-store-startup-" + std::to_string(::getpid()));
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
	for (unsigned changed = 0; changed < 6; ++changed) {
		const auto root = directory / std::to_string(changed);
		install(root, image, storage);
		write(root / "owner.lock", {});
		auto account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
		auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
		d::RetirementRuntime runtime(baseline, 1);
		auto domain = runtime.domain(account, 16);
		const auto initial = account->used();
		l::DiskStoreOptions options;
		options.limits = {16 * l::paged::segment_bytes, 100, 10};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account =
			std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
		auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
		require(store->status() == l::StoreStatus::Starting);
		const auto &segment = image.segments().front();
		const l::detail::SegmentNames names(segment.ownership.segment);
		if (changed == 1) {
			std::filesystem::rename(root / names.payload_name(), root / "retained.pack");
			write(root / names.payload_name(), {segment.bytes->data(), segment.bytes->size()});
		} else if (changed == 2) {
			auto ownership = segment.ownership;
			ownership.checksum[0] ^= 1;
			write(root / names.owner_name(), l::encode_segment_ownership(ownership));
		} else if (changed == 3) {
			std::filesystem::remove(root / names.payload_name());
		} else if (changed == 4) {
			std::filesystem::remove(root / names.owner_name());
		} else if (changed == 5) {
			const auto &descriptor = image.descriptor();
			write(root / (l::generation_name(generation) + ".lease"),
				  l::encode_generation_ownership(
					  {storage, generation, descriptor.length, descriptor.checksum, 1}));
		}
		const auto syncs = drive(store);
		if (changed == 0) {
			require(store->status() == l::StoreStatus::Ready && syncs == 6);
			require(store->selection().descriptor == image.descriptor());
			l::DiskUsage observed;
			for (const auto &entry : std::filesystem::directory_iterator(root)) {
				observed.bytes += entry.file_size();
				++observed.entries;
				if (entry.path().extension() == ".lease")
					++observed.generations;
			}
			require(store->stats().used == observed);
		} else {
			require(store->status() == l::StoreStatus::Fenced && bool(store->error()));
		}
		store->close();
		drive(store);
		store->advance();
		require(store->drained());
		store.reset();
		domain.close().wait();
		require(account->used() == initial);
	}
}
