#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_plan.hh"
#include "prepared_reader_fixture.hh"
#include "reader_capsule.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <thread>

using namespace prepared_test;
namespace {
void drive(const std::shared_ptr<l::DiskStore> &store) {
	for (unsigned turns = 0;; ++turns) {
		check(turns < 1'000'000);
		store->advance();
		auto job = store->operation();
		if (!job) {
			if (store->needs_work())
				continue;
			break;
		}
		job->submitted();
		check(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
	}
	if (store->error())
		std::rethrow_exception(store->error());
	if (store->collection_error())
		std::rethrow_exception(store->collection_error());
}
template <class Action> void rejected(Action action) {
	bool refused = false;
	try {
		action();
	} catch (const std::exception &) {
		refused = true;
	}
	check(refused);
}
std::filesystem::path metadata_path(const Fixture &f, l::GenerationId id) {
	return f.path / x::GenerationName(id, ".read").view();
}
void unlocked(const std::filesystem::path &path) {
	const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
	check(fd >= 0);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
		check(errno == EWOULDBLOCK || errno == EAGAIN);
		check(std::chrono::steady_clock::now() < deadline);
		std::this_thread::yield();
	}
	check(::close(fd) == 0);
}
void unpinned(const Fixture &f, l::GenerationId id) {
	unlocked(f.path / x::GenerationName(id, ".lease").view());
}
l::Descriptor republish(const Fixture &f, const std::shared_ptr<l::DiskStore> &store,
						d::RetirementDomain &domain, l::GenerationId next) {
	auto actual = x::ManagedRecovery::recover(f.path, x::ManagedRecovery::capture(*store));
	check(!x::PreparedReader::metadata_ready(f.path, actual, ::getuid(),
											 store->reader_metadata_encoding()));
	x::PagedPlanBuilder builder(x::PagedPlanBuilder::Republish{}, actual, next, domain);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto plan = builder.take();
	check(plan.segments().empty() && plan.root().tree == f.root.tree);
	const auto descriptor = plan.descriptor();
	check(descriptor.generation != actual.descriptor().generation &&
		  descriptor.cursor == actual.descriptor().cursor &&
		  descriptor.checksum == actual.descriptor().checksum);
	store->begin(std::move(plan));
	drive(store);
	check(store->publication_status() == l::DiskPublicationStatus::Published);
	auto current = x::ManagedRecovery::recover(f.path, x::ManagedRecovery::capture(*store));
	check(current.descriptor() == descriptor && current.view().get("a", "k").value() == 42);
	check(x::PreparedReader::metadata_ready(f.path, current, ::getuid(),
											store->reader_metadata_encoding()));
	store->confirm_recovered(current);
	check(store->stats().reserved == l::DiskUsage{});
	return descriptor;
}
void close_store(std::shared_ptr<l::DiskStore> &store) {
	store->close();
	drive(store);
	check(store->drained());
	store.reset();
}
} // namespace
void check_cli(const char *agent, const std::filesystem::path &path, bool valid) {
	const auto child = ::fork();
	check(child >= 0);
	if (!child) {
		::execl(agent, agent, "--verify-legacy-metadata", "--generation-dir", path.c_str(),
				static_cast<char *>(nullptr));
		::_exit(127);
	}
	int status = 0;
	while (::waitpid(child, &status, 0) < 0)
		check(errno == EINTR);
	check(WIFEXITED(status) && ((WEXITSTATUS(status) == 0) == valid));
}
int main(int argc, char **argv) {
	check(argc == 2);
	Fixture fixture;
	write_file(fixture.path / "owner.lock", {});
	auto baseline = std::make_shared<d::StorageLedger>(p::segment_bytes);
	auto heap = std::make_shared<d::StorageLedger>(128 * p::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(heap, 512);
	l::DiskStoreOptions options;
	options.limits = {128 * p::segment_bytes, 1000, 100};
	options.recovery_limits.retirement_domain = &domain;
	options.recovery_limits.mapping_account =
		std::make_shared<d::StorageLedger>(32 * p::segment_bytes);
	{
		auto legacy = x::PreparedReader::open(fixture.path, ::getuid(), options.recovery_limits);
		options.reader_metadata = l::ReaderMetadataEncoding::Capsule;
		auto store = l::make_disk_store(domain, fixture.path, heap, options);
		drive(store);
		l::GenerationId capsule_id{};
		capsule_id.fill('c');
		const auto capsule_descriptor = republish(fixture, store, domain, capsule_id);
		check(std::filesystem::file_size(metadata_path(fixture, capsule_id)) == 624);
		auto capsule = x::PreparedReader::open(fixture.path, ::getuid(), options.recovery_limits);
		check(capsule.descriptor == capsule_descriptor && capsule.view.eval("b", "k").value());
		std::array<char, x::reader_capsule_bytes> saved{};
		int fd = ::open(metadata_path(fixture, capsule_id).c_str(), O_RDONLY | O_CLOEXEC);
		check(fd >= 0 && ::pread(fd, saved.data(), saved.size(), 0) == ssize_t(saved.size()) &&
			  ::close(fd) == 0);
		{
			auto actual =
				x::ManagedRecovery::recover(fixture.path, x::ManagedRecovery::capture(*store));
			auto fake = x::decode_reader_capsule(saved);
			++fake.metadata.counts.strings;
			std::filesystem::remove(metadata_path(fixture, capsule_id));
			write_file(metadata_path(fixture, capsule_id), x::encode_reader_capsule(fake));
			rejected([&] { x::PreparedReader::metadata_ready(fixture.path, actual, ::getuid()); });
			std::filesystem::remove(metadata_path(fixture, capsule_id));
			write_file(metadata_path(fixture, capsule_id), saved);
		}
		check_cli(argv[1], fixture.path, false);
		close_store(store);
		unlocked(fixture.path / "owner.lock");
		options.reader_metadata = l::ReaderMetadataEncoding::Legacy;
		store = l::make_disk_store(domain, fixture.path, heap, options);
		drive(store);
		l::GenerationId rollback_id{};
		rollback_id.fill('r');
		republish(fixture, store, domain, rollback_id);
		check(std::filesystem::file_size(metadata_path(fixture, rollback_id)) == 192);
		store->request_collection();
		drive(store);
		check(std::filesystem::exists(metadata_path(fixture, capsule_id)));
		check(capsule.view.get("a", "k").value() == 42);
		rejected([&] { l::make_owned_storage_session(domain, fixture.path, heap, false); });
		close_store(store);
		unlocked(fixture.path / "owner.lock");
		{
			auto session = l::make_owned_storage_session(domain, fixture.path, heap, false);
			rejected([&] { l::verify_legacy_reader_metadata(fixture.path, *session); });
			session->close();
		}
		unlocked(fixture.path / "owner.lock");
		check_cli(argv[1], fixture.path, false);
		legacy = {};
		capsule = {};
		unpinned(fixture, fixture.generation);
		unpinned(fixture, capsule_id);
		// Recovery and collection retain corrupt unselected artifacts through rollback.
		auto corrupt = saved;
		corrupt.back() ^= 1;
		std::filesystem::remove(metadata_path(fixture, capsule_id));
		write_file(metadata_path(fixture, capsule_id), corrupt);
		store = l::make_disk_store(domain, fixture.path, heap, options);
		drive(store);
		store->request_collection();
		drive(store);
		check(std::filesystem::exists(metadata_path(fixture, capsule_id)));
		close_store(store);
		unlocked(fixture.path / "owner.lock");
		{
			auto session = l::make_owned_storage_session(domain, fixture.path, heap, false);
			rejected([&] { l::verify_legacy_reader_metadata(fixture.path, *session); });
			session->close();
		}
		unlocked(fixture.path / "owner.lock");
		std::filesystem::remove(metadata_path(fixture, capsule_id));
		write_file(metadata_path(fixture, capsule_id), saved);
		store = l::make_disk_store(domain, fixture.path, heap, options);
		drive(store);
		store->request_collection();
		drive(store);
		check(!std::filesystem::exists(metadata_path(fixture, capsule_id)));
		check(!std::filesystem::exists(fixture.path /
									   x::GenerationName(capsule_id, ".lease").view()));
		close_store(store);
		unlocked(fixture.path / "owner.lock");
		{
			auto session = l::make_owned_storage_session(domain, fixture.path, heap, false);
			l::verify_legacy_reader_metadata(fixture.path, *session);
			session->close();
		}
		unlocked(fixture.path / "owner.lock");
		check_cli(argv[1], fixture.path, true);
		check(x::PreparedReader::open(fixture.path, ::getuid(), options.recovery_limits)
				  .view.get("a", "k")
				  .value() == 42);
	}
	domain.close().wait();
}
