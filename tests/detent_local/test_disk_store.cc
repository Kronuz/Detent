// DiskStore over the agent store (docs/status/namespace-s7.md "Wiring"): startup modes, publication
// settled against the store's census, cancellation, admission, collection around a reader's lease, a
// close while work runs, and the release of a settled publication's image.
#include "detent_local/disk_store.hh"
#include "detent_local/reader.hh"
#include "snapshot_fixture.hh"
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
namespace l = detent::local;
namespace d = detent::distribution;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("disk store invariant at line " + std::to_string(where.line()));
}
void check_released(const std::shared_ptr<d::StorageLedger> &account) {
	// The runtime may still hold its charged domain control until its current registry pass finishes.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (account->used() && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	check(!account->used());
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
std::unique_ptr<l::ImageBuilder> build(std::shared_ptr<d::StorageLedger> account, char generation,
									   std::uint64_t revision, std::uint32_t key_padding = 0) {
	auto expected = cursor();
	expected.revision = revision;
	d::Limits limits;
	limits.storage_account = std::move(account);
	d::SnapshotDecoder decoder(expected, {"app"}, limits);
	auto encoded = fixture(1, 24 + key_padding, 25, revision);
	if (key_padding) {
		check(key_padding <= 23);
		const auto offset = encoded.find("app:flag");
		check(offset != encoded.npos && offset > 0);
		encoded[offset - 1] = static_cast<char>(0xa8 + key_padding);
		encoded.insert(offset + 8, key_padding, 'x');
	}
	auto input = std::span<const char>(encoded.data(), encoded.size());
	while (!input.empty()) {
		d::WorkBudget budget;
		decoder.feed(input, budget);
	}
	auto source = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
	auto builder = std::make_unique<l::ImageBuilder>(source);
	while (!builder->done()) {
		l::ValidationBudget budget;
		builder->step(budget);
	}
	return builder;
}
// The census the agent store keeps: every name, one generation per `.lease`.
l::DiskUsage census(const std::filesystem::path &directory) {
	l::DiskUsage usage;
	for (const auto &entry : std::filesystem::directory_iterator(directory)) {
		++usage.entries;
		struct stat info{};
		check(::lstat(entry.path().c_str(), &info) == 0 && info.st_size >= 0);
		usage.bytes += static_cast<std::uint64_t>(info.st_size);
		if (entry.path().extension() == ".lease")
			++usage.generations;
	}
	return usage;
}
std::filesystem::path fresh(const std::filesystem::path &root, std::string_view name) {
	const auto path = root / name;
	std::filesystem::create_directories(path);
	std::filesystem::permissions(path, std::filesystem::perms::owner_all);
	return path;
}
struct Store {
	d::RetirementDomain domain;
	std::shared_ptr<l::DiskStore> store;
	Store(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
		  const std::filesystem::path &directory, l::StoreOpenMode mode, l::DiskLimits limits = {1 << 20, 100, 10})
		: domain(runtime.domain(account, 1)) {
		l::DiskStoreOptions options;
		options.mode = mode;
		options.limits = limits;
		try {
			store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		} catch (...) {
			domain.close().wait();
			throw;
		}
	}
	~Store() {
		store->close();
		drive(store);
		check(store->drained());
		store.reset();
		domain.close().wait();
	}
	void publish(const l::ImageBuilder &builder) {
		const auto base = store->selection().descriptor;
		store->begin(builder, base ? std::optional(base->cursor) : std::nullopt);
		drive(store);
		check(store->publication_status() == l::DiskPublicationStatus::Published);
		check(store->selection().descriptor == builder.descriptor());
	}
};
void startup_modes(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
				   const std::filesystem::path &root) {
	const auto empty = fresh(root, "modes");
	// An existing store needs its `owner.lock`; a fresh one needs an empty directory.
	bool refused = false;
	try {
		Store missing(runtime, account, empty, l::StoreOpenMode::ExistingManaged);
	} catch (const std::exception &) {
		refused = true;
	}
	check(refused && std::filesystem::is_empty(empty));
	{
		Store created(runtime, account, empty, l::StoreOpenMode::FreshEmpty);
		check(created.store->status() == l::StoreStatus::Ready && !created.store->error());
		check(created.store->selection().knowledge == l::SelectionKnowledge::Absent);
		check(std::filesystem::exists(empty / "owner.lock") && std::filesystem::exists(empty / "change"));
		check(created.store->stats().used == census(empty) && created.store->stats().reserved == l::DiskUsage{});
	}
	refused = false;
	try {
		Store again(runtime, account, empty, l::StoreOpenMode::FreshEmpty);
	} catch (const std::exception &) {
		refused = true;
	}
	check(refused);
	Store reopened(runtime, account, empty, l::StoreOpenMode::ExistingManaged);
	check(reopened.store->status() == l::StoreStatus::Ready &&
		  reopened.store->selection().knowledge == l::SelectionKnowledge::Absent);
}
void publication_and_collection(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
								const std::filesystem::path &root) {
	const auto directory = fresh(root, "publish");
	std::optional<l::Descriptor> last;
	{
		Store s(runtime, account, directory, l::StoreOpenMode::FreshEmpty);
		auto first = build(account, 'a', 42);
		s.publish(*first);
		check(s.store->stats().used == census(directory) && s.store->stats().reserved == l::DiskUsage{});
		check(l::read_generation_selection(directory).descriptor == first->descriptor());
		check(s.store->stats().used.generations == 1);
		// A reader holds the first generation while the second is published.
		const auto name = l::generation_name(first->descriptor().generation) + ".lease";
		const int lease = ::open((directory / name).c_str(), O_RDONLY | O_CLOEXEC);
		check(lease >= 0 && ::flock(lease, LOCK_SH | LOCK_NB) == 0);
		auto second = build(account, 'b', 43);
		s.publish(*second);
		check(std::filesystem::exists(directory / name) && s.store->stats().used.generations == 2);
		check(s.store->stats().used == census(directory) && !s.store->collection_error());
		check(::close(lease) == 0);
		s.store->request_collection();
		drive(s.store);
		check(!std::filesystem::exists(directory / name) && s.store->stats().used.generations == 1);
		check(s.store->stats().used == census(directory) && !s.store->collection_error());
		const auto peak = s.store->stats().peak;
		check(peak.generations == 2 && peak.bytes >= s.store->stats().used.bytes);
		last = second->descriptor();
	}
	Store reopened(runtime, account, directory, l::StoreOpenMode::ExistingManaged);
	check(reopened.store->status() == l::StoreStatus::Ready);
	check(reopened.store->selection().knowledge == l::SelectionKnowledge::Known &&
		  reopened.store->selection().descriptor == last);
	check(reopened.store->stats().used == census(directory));
	// The next publication must move forward from the selected cursor.
	auto stale = build(account, 'c', 43);
	bool refused = false;
	try {
		reopened.store->begin(*stale, last->cursor);
	} catch (const std::invalid_argument &) {
		refused = true;
	}
	check(refused && reopened.store->publication_ready());
}
void cancellation_and_admission(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
								const std::filesystem::path &root) {
	const auto directory = fresh(root, "cancel");
	{
		Store s(runtime, account, directory, l::StoreOpenMode::FreshEmpty);
		const auto before = census(directory);
		auto first = build(account, 'a', 42);
		s.store->begin(*first);
		check(!s.store->publication_ready() && s.store->stats().reserved != l::DiskUsage{});
		s.store->cancel_publication();
		check(s.store->publication_status() == l::DiskPublicationStatus::Cancelled);
		check(s.store->stats().reserved == l::DiskUsage{} && s.store->publication_ready());
		drive(s.store);
		check(census(directory) == before && s.store->selection().knowledge == l::SelectionKnowledge::Absent);
		// Taken work is not cancelled: it runs and settles.
		s.store->begin(*first);
		auto work = s.store->work();
		check(work && !s.store->work());
		s.store->cancel_publication();
		work->run();
		s.store->advance();
		check(s.store->publication_status() == l::DiskPublicationStatus::Published);
	}
	const auto tight = fresh(root, "tight");
	Store s(runtime, account, tight, l::StoreOpenMode::FreshEmpty, {64, 100, 10});
	auto first = build(account, 'a', 42);
	bool pressure = false;
	try {
		s.store->begin(*first);
	} catch (const l::DiskPressure &) {
		pressure = true;
	}
	check(pressure && s.store->publication_ready() && s.store->stats().reserved == l::DiskUsage{});
	check(!s.store->work());
}
void close_while_running(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
						 const std::filesystem::path &root) {
	const auto directory = fresh(root, "close");
	auto domain = runtime.domain(account, 1);
	l::DiskStoreOptions options;
	options.mode = l::StoreOpenMode::FreshEmpty;
	options.limits = {1 << 20, 100, 10};
	auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
	auto builder = build(account, 'a', 42);
	std::weak_ptr<const d::OwnedBuffer> image = builder->bytes();
	store->begin(*builder);
	builder.reset();
	auto work = store->work();
	check(work && !image.expired());
	store->close();
	check(store->status() == l::StoreStatus::Closing && !store->drained());
	std::thread worker([&] { work->run(); });
	worker.join();
	store->advance();
	check(store->publication_status() == l::DiskPublicationStatus::Published && store->drained());
	// The settled store keeps nothing of the publication, though the taken work is still held.
	check(image.expired());
	work.reset();
	store.reset();
	domain.close().wait();
	check(l::read_generation_selection(directory).descriptor.cursor.revision == 42);
}
} // namespace
int main() {
	try {
		const auto root =
			std::filesystem::current_path() / ".scratch" / ("disk-store-" + std::to_string(::getpid()));
		std::filesystem::create_directories(root);
		std::filesystem::permissions(root, std::filesystem::perms::owner_all);
		auto baseline = std::make_shared<d::StorageLedger>(1024 * 1024);
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		d::RetirementRuntime runtime(baseline, 2);
		startup_modes(runtime, account, root);
		check_released(account);
		publication_and_collection(runtime, account, root);
		check_released(account);
		cancellation_and_admission(runtime, account, root);
		check_released(account);
		close_while_running(runtime, account, root);
		check_released(account);
		std::filesystem::remove_all(root);
		runtime.shutdown();
		std::cout << "startup modes, census settlement, cancellation, admission, collection around a lease and "
					 "close while running passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
