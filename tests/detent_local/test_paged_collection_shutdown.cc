#include "detent_local/disk_store.hh"
#include "paged_disk_fixture.hh"
#include <completion/native_completion.h>
#include <iostream>
#include <limits>
namespace c = kronuz::io::completion;
int main() {
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("paged-collection-shutdown-" + std::to_string(::getpid()));
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
	auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 256);
	auto exercise = [&](std::size_t stop_read, bool inject) {
		std::filesystem::remove_all(directory);
		install(directory, image, storage);
		write(directory / "owner.lock", std::span<const char>{});
		l::StorageIdentity orphan{};
		orphan.fill('z');
		const l::detail::SegmentNames names(orphan);
		const std::string bytes(257, 'x');
		write(directory / names.owner_name(),
			  l::encode_segment_ownership(
				  {storage, orphan, bytes.size(), l::paged::physical_commitment(bytes)}));
		write(directory / names.payload_name(), bytes);
		l::DiskStoreOptions options;
		options.mode = l::StoreOpenMode::ExistingManaged;
		options.recovery_limits.retirement_domain = &domain;
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		for (unsigned turns = 0; store->operation(); ++turns) {
			require(turns < 10000);
			auto operation = store->operation();
			operation->submitted();
			require(operation->complete(
				kronuz::journal::detail::execute_primitive(operation->io(), operation->request())));
			store->advance();
		}
		require(store->status() == l::StoreStatus::Ready);
		store->request_collection();
		store->advance();
		const auto failure = std::make_exception_ptr(std::runtime_error("shutdown original read failure"));
		std::shared_ptr<c::Operation> tail;
		std::size_t reads = 0;
		bool closed = false;
		for (unsigned turns = 0; store->operation() || store->needs_work(); ++turns) {
			require(turns < 100000);
			auto operation = store->operation();
			if (!operation) {
				store->advance();
				continue;
			}
			const auto request = operation->request();
			operation->submitted();
			const bool stop = request.kind == c::Kind::Read && reads++ == stop_read;
			if (stop) {
				store->close();
				closed = true;
			}
			c::Result result;
			if (stop && inject) {
				result.token = request.token;
				result.error = failure;
			} else
				result = kronuz::journal::detail::execute_primitive(operation->io(), request);
			require(operation->complete(std::move(result)));
			if (closed && operation->done())
				tail = operation;
			store->advance();
		}
		require(!store->stats().fenced);
		if (stop_read != std::numeric_limits<std::size_t>::max()) {
			require(closed && store->drained());
			require(tail && tail->io().inspect_entry("selected").has_value());
			tail.reset();
			if (inject)
				require(store->collection_error() == failure);
		} else {
			require(!store->collection_error());
			store->close();
			while (auto operation = store->operation()) {
				operation->submitted();
				require(operation->complete(
					kronuz::journal::detail::execute_primitive(operation->io(), operation->request())));
				store->advance();
			}
			require(store->drained());
		}
		return reads;
	};
	const auto reads = exercise(std::numeric_limits<std::size_t>::max(), false);
	require(reads >= 6);
	for (std::size_t read = 0; read < reads; ++read) {
		exercise(read, false);
		exercise(read, true);
	}
	std::cout << "Accepted collection read boundaries: " << reads << " (success and error shutdown)\n";
	domain.close().wait();
}
