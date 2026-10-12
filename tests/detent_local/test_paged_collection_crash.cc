#include "paged_disk_fixture.hh"
#include "paged_segment_collection.hh"
#include "store_core.hh"
#include <completion/native_completion.h>
#include <csignal>
#include <sys/wait.h>
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
int main() {
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("paged-collection-crash-" + std::to_string(::getpid()));
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
	l::StorageIdentity orphan{};
	orphan.fill('z');
	const l::detail::SegmentNames names(orphan);
	const std::string bytes(257, 'x');
	for (unsigned boundary = 0; boundary < 4; ++boundary) {
		const auto root = directory / std::to_string(boundary);
		install(root, image, storage);
		write(root / names.owner_name(), l::encode_segment_ownership({storage, orphan, bytes.size(),
																	  l::paged::physical_commitment(bytes)}));
		write(root / names.payload_name(), bytes);
		const auto child = ::fork();
		require(child >= 0);
		if (child == 0) {
			try {
				auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
				auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
				d::RetirementRuntime runtime(baseline, 1);
				auto domain = runtime.domain(account, 64);
				auto core =
					std::make_shared<l::detail::StoreCore>(account, l::DiskStoreOptions{}, domain.factory());
				core->session = l::make_owned_storage_session(domain, root, std::make_shared<int>(1), true);
				core->controls = l::make_collector_controls(core->session);
				core->directory_identity = core->session->directory_identity();
				core->identity = storage;
				core->owner = ::getuid();
				core->selection = {l::SelectionKnowledge::Known, image.descriptor()};
				core->status = l::StoreStatus::Ready;
				core->ledger.initialize(census(root));
				l::detail::PagedSegmentCollection collector(domain.factory(), core);
				collector.start();
				unsigned mutations = 0;
				for (unsigned turns = 0; !collector.done(); ++turns) {
					require(turns < 100000);
					if (collector.paused()) {
						collector.resume();
						continue;
					}
					if (collector.needs_work()) {
						l::ValidationBudget budget;
						collector.step(budget);
						continue;
					}
					collector.advance();
					if (collector.done() || collector.needs_work())
						continue;
					const auto request = collector.request();
					collector.submitted();
					require(collector.complete(
						kronuz::journal::detail::execute_primitive(collector.io(), request)));
					if (request.kind == c::Kind::Remove || request.kind == c::Kind::DirectorySync) {
						if (mutations++ == boundary)
							::kill(::getpid(), SIGKILL);
					}
				}
			} catch (...) {
				::_exit(91);
			}
			::_exit(92);
		}
		int status = 0;
		require(::waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
		require(!std::filesystem::exists(root / names.payload_name()));
		require(std::filesystem::exists(root / names.owner_name()) == (boundary < 2));
		{
			auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
			auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
			d::RetirementRuntime runtime(baseline, 1);
			auto domain = runtime.domain(account, 128);
			l::DiskStoreOptions options;
			options.recovery_limits.retirement_domain = &domain;
			auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
			auto drive = [&] {
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
			};
			drive();
			require(store->status() == l::StoreStatus::Ready && store->stats().used == census(root));
			store->request_collection();
			store->advance();
			drive();
			require(!store->collection_error() && !store->stats().fenced &&
					store->stats().used == census(root));
			require(!std::filesystem::exists(root / names.owner_name()));
			{
				auto selected = l::recover_generation(root, options.recovery_limits);
				require(selected.view().get("app", "count").value() == INT64_MIN);
			}
			store->close();
			drive();
			require(store->drained());
			store.reset();
			domain.close().wait();
		}
	}
}
