#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_disk_fixture.hh"
#include "paged_maintenance_plan.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <fcntl.h>
#include <source_location>
#include <sys/file.h>
#include <thread>
namespace c = kronuz::io::completion;
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("maintenance invariant at " + std::to_string(location.line()));
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
void run(bool paged_base) {
	const auto directory =
		std::filesystem::current_path() / ".scratch" /
		("paged-maintenance-" + std::to_string(::getpid()) + "-" + std::to_string(paged_base));
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
		l::GenerationId original{}, next{};
		original.fill('f');
		next.fill('p');
		l::StorageIdentity storage{};
		storage.fill('s');
		if (paged_base) {
			l::detail::PagedSnapshotBuilder builder(source, storage, original);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			install(directory, builder.take(), storage);
		} else {
			l::ImageBuilder builder(source, original);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			std::filesystem::create_directories(directory);
			check(::chmod(directory.c_str(), 0700) == 0);
			const auto name = l::generation_name(original);
			write(directory / (name + ".image"), *builder.bytes());
			write(directory / "storage.meta", l::encode_storage_identity(storage));
			write(directory / (name + ".lease"),
				  l::encode_generation_ownership(
					  {storage, original, builder.descriptor().length, builder.descriptor().checksum, 1}));
			write(directory / (name + ".descriptor"), l::encode_descriptor(builder.descriptor()));
			write(directory / "selected", l::encode_descriptor(builder.descriptor()));
		}
		write(directory / "owner.lock", {});
		l::DiskStoreOptions options;
		options.limits = {64 * l::paged::segment_bytes, 1000, 10};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = mappings;
		auto base = l::recover_generation(directory, options.recovery_limits);
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		if (store->error())
			std::rethrow_exception(store->error());
		check(store->status() == l::StoreStatus::Ready);
		l::detail::PagedMaintenanceBuilder builder(base, domain.factory(), heap,
												   l::detail::ManagedRecovery::capture(*store).storage, next,
												   options.recovery_limits);
		unsigned turns = 0;
		while (!builder.done()) {
			l::ValidationBudget budget{1, 1};
			builder.step(budget);
			check(++turns < 1000000);
		}
		auto plan = builder.take();
		const auto wanted = plan.descriptor();
		check(plan.maintenance() && !plan.registry() && wanted.cursor == base.descriptor().cursor);
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
			check(l::read_generation_selection(directory).descriptor == base.descriptor());
			check(!std::filesystem::exists(directory / (l::generation_name(next) + ".image")));
		}
		l::GenerationId stale_id{};
		stale_id.fill('q');
		l::detail::PagedMaintenanceBuilder stale_builder(base, domain.factory(), heap,
														 l::detail::ManagedRecovery::capture(*store).storage,
														 stale_id, options.recovery_limits);
		while (!stale_builder.done()) {
			l::ValidationBudget budget;
			stale_builder.step(budget);
		}
		auto stale = stale_builder.take();
		store->begin(std::move(plan));
		drive(store);
		check(store->publication_status() == l::DiskPublicationStatus::Published &&
			  store->selection().descriptor == wanted);
		auto recovered =
			l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store));
		check(recovered.descriptor() == wanted && recovered.view().get("app", "count").value() == INT64_MIN);
		store->confirm_recovered(recovered);
		const auto stale_descriptor = stale.descriptor();
		bool refused = false;
		try {
			store->begin(std::move(stale));
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		check(refused && stale.descriptor() == stale_descriptor && store->selection().descriptor == wanted);
		check(store->stats().reserved == l::DiskUsage{});
		check(!std::filesystem::exists(directory / (l::generation_name(stale_id) + ".image")));
		check(base.view().get("app", "count").value() == INT64_MIN);
		check(recovered.view().eval("app", "flag", "host-a", "beta").value() ==
			  base.view().eval("app", "flag", "host-a", "beta").value());
		store->close();
		drive(store);
		check(store->drained());
		store.reset();
		const auto lock = ::open((directory / "owner.lock").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
		check(lock >= 0);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (::flock(lock, LOCK_EX | LOCK_NB) != 0) {
			check((errno == EWOULDBLOCK || errno == EAGAIN) && std::chrono::steady_clock::now() < deadline);
			std::this_thread::yield();
		}
		check(::flock(lock, LOCK_UN) == 0 && ::close(lock) == 0);
		auto restarted = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(restarted);
		check(restarted->status() == l::StoreStatus::Ready && restarted->selection().descriptor == wanted);
		restarted->close();
		drive(restarted);
	}
	domain.close().wait();
	check(mappings->used() == 0);
}
int main() {
	run(false);
	run(true);
}
