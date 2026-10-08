#include "detent_distribution/memory_resource.hh"
#include "detent_local/disk_store.hh"
#include "detent_local/publication.hh"
#include "snapshot_fixture.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <completion/operation.h>
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <spawn.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
namespace l = detent::local;
namespace d = detent::distribution;
namespace c = kronuz::io::completion;
extern char **environ;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("disk store invariant at line " + std::to_string(where.line()));
}
void check_usage(const std::shared_ptr<d::StorageLedger> &account, std::size_t expected) {
	// Drain settles native residents. The runtime may still hold its charged
	// domain control until its current registry pass finishes.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (account->used() != expected && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	check(account->used() == expected);
}
void check_released(const std::shared_ptr<d::StorageLedger> &account) { check_usage(account, 0); }
void drive(const std::shared_ptr<l::DiskStore> &store) {
	while (auto job = store->operation()) {
		job->submitted();
		check(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
		store->advance();
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
	l::GenerationId id;
	id.fill(generation);
	auto builder = std::make_unique<l::ImageBuilder>(source, id);
	while (!builder->done()) {
		l::ValidationBudget budget;
		builder->step(budget);
	}
	return builder;
}
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
void write_file(const std::filesystem::path &path, std::span<const char> bytes = {}) {
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!bytes.empty())
		stream.write(bytes.data(), bytes.size());
	stream.close();
	check(static_cast<bool>(stream));
	std::filesystem::permissions(path,
								 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
void startup_profiles(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
					  const std::filesystem::path &parent) {
	for (unsigned scenario = 0; scenario < 12; ++scenario) {
		const auto directory = parent / ("profile-" + std::to_string(scenario));
		std::filesystem::create_directory(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		l::DiskStoreOptions options;
		options.mode = l::StoreOpenMode::UpgradeLegacy;
		options.limits = {1024 * 1024, 100, 10};
		if (scenario)
			write_file(directory / "owner.lock");
		l::GenerationId generation;
		generation.fill('l');
		const auto lease = directory / (l::generation_name(generation) + ".lease");
		if (scenario == 2 || scenario == 3 || scenario == 4 || scenario == 5)
			write_file(directory / "foreign");
		if (scenario == 3 || scenario == 4 || scenario == 5)
			write_file(lease);
		if (scenario == 4) {
			const std::array<char, 1> nonempty{'x'};
			write_file(lease, nonempty);
		}
		if (scenario == 5)
			std::filesystem::create_hard_link(lease, directory / "alias");
		if (scenario >= 6 && scenario <= 9) {
			l::StorageIdentity identity;
			identity.fill('i');
			auto metadata = l::encode_storage_identity(identity);
			if (scenario == 6)
				metadata.back() ^= 1;
			if (scenario == 7)
				write_file(directory / "storage.meta", std::span(metadata).first(63));
			else
				write_file(directory / "storage.meta", metadata);
			if (scenario == 8)
				std::filesystem::create_hard_link(directory / "storage.meta", directory / "alias");
			if (scenario == 9)
				std::filesystem::permissions(directory / "storage.meta", std::filesystem::perms::all);
		}
		if (scenario == 10)
			options.mode = l::StoreOpenMode::ExistingManaged;
		if (scenario == 11)
			options.limits.bytes = 63;
		auto domain = runtime.domain(account, 1);
		if (!scenario) {
			bool rejected = false;
			try {
				l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
			} catch (const std::exception &) {
				rejected = true;
			}
			check(rejected && !std::filesystem::exists(directory / "owner.lock"));
		} else {
			auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
			drive(store);
			const bool ready = scenario == 1 || scenario == 3;
			check(store->status() == (ready ? l::StoreStatus::Ready : l::StoreStatus::Fenced));
			if (ready) {
				check(store->stats().used == census(directory));
				const auto before = store->stats().used;
				store->request_collection();
				store->advance();
				drive(store);
				check(store->stats().used == before); // Empty legacy anchors grant no managed GC authority.
			} else if (scenario < 6 || scenario > 9)
				check(!std::filesystem::exists(directory / "storage.meta"));
			store->close();
			store.reset();
		}
		domain.close().wait();
	}
	std::cout << "startup profile/evidence cases: 12\n";
}
void recovery_durability(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
						 const std::filesystem::path &parent) {
	auto builder = build(account, 'u', 42);
	const auto injected = std::make_exception_ptr(std::runtime_error("injected recovery sync failure"));
	for (bool legacy : {false, true}) {
		for (unsigned fault = 0; fault <= 4; ++fault) {
			const auto directory =
				parent / ("recovery-sync-" + std::to_string(legacy) + "-" + std::to_string(fault));
			std::filesystem::create_directory(directory);
			std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			l::DiskStoreOptions options;
			options.mode = l::StoreOpenMode::FreshEmpty;
			options.limits = {1024 * 1024, 100, 10};
			{
				auto domain = runtime.domain(account, 1);
				auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
				drive(store);
				store->begin(*builder);
				drive(store);
				check(store->publication_status() == l::DiskPublicationStatus::Published);
				store->close();
				store.reset();
				domain.close().wait();
			}
			if (legacy) {
				std::filesystem::remove(directory / "storage.meta");
				write_file(directory / (l::generation_name(builder->descriptor().generation) + ".lease"));
				options.mode = l::StoreOpenMode::UpgradeLegacy;
				auto domain = runtime.domain(account, 1);
				auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
				drive(store);
				check(store->status() == l::StoreStatus::Ready && store->stats().used == census(directory));
				store->close();
				store.reset();
				domain.close().wait();
			}
			options.mode = l::StoreOpenMode::ExistingManaged;
			const auto before = census(directory);
			auto domain = runtime.domain(account, 1);
			auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
			unsigned syncs = 0;
			bool fired = false;
			while (auto job = store->operation()) {
				check(store->selection().knowledge == l::SelectionKnowledge::Uncertain);
				const auto kind = job->request().kind;
				job->submitted();
				auto result = kronuz::journal::detail::execute_primitive(job->io(), job->request());
				check(!result.error);
				if (kind == c::Kind::Sync && ++syncs == fault) {
					result.error = injected;
					fired = true;
				}
				if (kind == c::Kind::DirectorySync)
					check(syncs == 4 && !fired && store->status() == l::StoreStatus::Starting);
				check(job->complete(std::move(result)));
				store->advance();
			}
			check(census(directory) == before);
			check(store->status() == (fault ? l::StoreStatus::Fenced : l::StoreStatus::Ready));
			check(fired == bool(fault));
			if (fault)
				check(store->selection().knowledge == l::SelectionKnowledge::Uncertain);
			if (!fault) {
				check(syncs == 4 && store->selection().descriptor == builder->descriptor());
				if (legacy) {
					auto next = build(account, 'v', 43);
					store->begin(*next, builder->descriptor().cursor);
					drive(store);
					const auto retained = census(directory);
					store->request_collection();
					store->advance();
					drive(store);
					check(store->status() == l::StoreStatus::Ready && store->stats().used == retained);
					check(std::filesystem::exists(
						directory / (l::generation_name(builder->descriptor().generation) + ".image")));
				}
			}
			store->close();
			drive(store);
			check(store->drained());
			store.reset();
			domain.close().wait();
		}
	}
	std::cout << "managed and legacy recovery sync cases: 10\n";
}
void facade_cancellation(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
						 const std::filesystem::path &parent) {
	auto first = build(account, 'x', 42);
	auto second = build(account, 'y', 43);
	auto third = build(account, 'z', 44);
	for (bool close : {false, true}) {
		for (bool barrier : {false, true}) {
			const auto directory =
				parent / ("facade-cancel-" + std::to_string(close) + "-" + std::to_string(barrier));
			std::filesystem::create_directory(directory);
			std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			auto domain = runtime.domain(account, 1);
			l::DiskStoreOptions options;
			options.mode = l::StoreOpenMode::FreshEmpty;
			options.limits = {1024 * 1024, 100, 10};
			auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
			auto publisher = std::make_unique<l::ImagePublisher>(store);
			auto run = [&] {
				while (auto job = publisher->operation()) {
					job->submitted();
					check(
						job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
					publisher->advance();
				}
			};
			run();
			publisher->begin(*first);
			run();
			check(publisher->published_cursor() == first->descriptor().cursor);
			publisher->begin(*second, first->descriptor().cursor);
			bool replaced = false, cancelled = false;
			while (auto job = publisher->operation()) {
				const auto kind = job->request().kind;
				job->submitted();
				auto result = kronuz::journal::detail::execute_primitive(job->io(), job->request());
				check(!result.error);
				if ((!barrier && kind == c::Kind::Replace) ||
					(barrier && replaced && kind == c::Kind::DirectorySync)) {
					if (close)
						publisher->close();
					else
						publisher->cancel();
					cancelled = true;
				}
				replaced |= kind == c::Kind::Replace;
				check(job->complete(std::move(result)));
				publisher->advance();
			}
			check(cancelled && store->selection().descriptor == second->descriptor());
			check(store->stats().used == census(directory) && store->stats().reserved == l::DiskUsage{});
			check(publisher->published_cursor() == first->descriptor().cursor);
			check(publisher->state() ==
				  (close ? l::PublicationState::Closed : l::PublicationState::Cancelled));
			if (!close) {
				publisher->begin(*third, second->descriptor().cursor);
				run();
				check(publisher->state() == l::PublicationState::Published &&
					  publisher->published_cursor() == third->descriptor().cursor);
			}
			publisher->close();
			run();
			publisher.reset();
			store.reset();
			domain.close().wait();
			if (!close) {
				options.mode = l::StoreOpenMode::ExistingManaged;
				auto recovery_domain = runtime.domain(account, 1);
				auto recovery_store =
					l::make_disk_store(recovery_domain, directory, std::make_shared<int>(1), options);
				auto recovery_publisher = std::make_unique<l::ImagePublisher>(recovery_store);
				check(recovery_publisher->state() == l::PublicationState::Recovering);
				recovery_publisher->cancel();
				while (auto job = recovery_publisher->operation()) {
					job->submitted();
					check(
						job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
					recovery_publisher->advance();
				}
				check(recovery_store->status() == l::StoreStatus::Ready &&
					  recovery_store->selection().descriptor == third->descriptor());
				check(recovery_publisher->state() == l::PublicationState::Cancelled &&
					  !recovery_publisher->published_cursor());
				recovery_publisher->close();
				recovery_publisher.reset();
				recovery_store.reset();
				recovery_domain.close().wait();
			}
		}
	}
	std::cout << "managed facade cancellation/acknowledgement cases: 6\n";
}
void publication_admission(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
						   const std::filesystem::path &parent) {
	const auto directory = parent / "admission";
	std::filesystem::create_directory(directory);
	std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
	auto builder = build(account, 'q', 42);
	const auto source_baseline = account->used();
	l::DiskStoreOptions options;
	options.mode = l::StoreOpenMode::FreshEmpty;
	options.limits = {1024 * 1024, 100, 10};
	std::size_t capacity = 0;
	{
		auto domain = runtime.domain(account, 1);
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		const auto baseline = account->used();
		store->begin(*builder);
		capacity = account->used() - baseline;
		check(capacity > 0);
		store->cancel_publication();
		store->close();
		store.reset();
		domain.close().wait();
	}
	options.mode = l::StoreOpenMode::ExistingManaged;
	check_usage(account, source_baseline);
	{
		auto domain = runtime.domain(account, 1);
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		const auto baseline = account->used();
		for (std::size_t available = 0; available <= capacity; ++available) {
			auto pressure = account->admit(account->limit() - baseline - available);
			bool admitted = false;
			try {
				store->begin(*builder);
				admitted = true;
			} catch (const d::StoragePressure &) {
			} catch (const std::bad_alloc &) {
			}
			check(admitted == (available == capacity));
			check(census(directory) == l::DiskUsage{64, 2, 0});
			if (admitted)
				store->cancel_publication();
			check(store->stats().reserved == l::DiskUsage{});
			pressure.reset();
			if (!admitted)
				check(account->used() == baseline);
		}
		store->close();
		store.reset();
		domain.close().wait();
	}
	// Reopening under an observed over-quota root preserves debt and blocks
	// publication before any family effect, while permitting safe collection.
	options.limits = {63, 1, 0};
	{
		auto domain = runtime.domain(account, 1);
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		check(store->status() == l::StoreStatus::Ready && store->stats().used == l::DiskUsage{64, 2, 0});
		const auto baseline = account->used();
		bool rejected = false;
		try {
			store->begin(*builder);
		} catch (const l::DiskPressure &) {
			rejected = true;
		}
		check(rejected && account->used() == baseline && census(directory) == l::DiskUsage{64, 2, 0});
		store->close();
		store.reset();
		domain.close().wait();
	}
	std::cout << "publication admission sweep: " << capacity << " failures, 1 success\n";
}
void quarantine_families(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
						 const std::filesystem::path &parent) {
	auto first = build(account, 'a', 42, 8);
	auto second = build(account, 'b', 43);
	// Contradictory first/last available bytes of every bound partial field.
	constexpr std::array image_offsets{16u, 22u, 40u, 47u,	48u,  55u,	56u,  63u,	64u,
									   71u, 72u, 79u, 168u, 175u, 176u, 182u, 184u, 190u};
	constexpr std::array descriptor_offsets{0u,	 15u,  16u,	 31u,  32u,	 38u,  40u,
											71u, 160u, 163u, 164u, 167u, 168u, 174u};
	constexpr std::array identity_offsets{88u, 104u, 120u, 136u, 72u, 88u, 104u, 120u};
	constexpr auto prefix_scenarios = 18 + image_offsets.size() + descriptor_offsets.size();
	constexpr auto scenarios = prefix_scenarios + identity_offsets.size();
	for (unsigned scenario = 0; scenario < scenarios; ++scenario) {
		const auto directory = parent / ("quarantine-" + std::to_string(scenario));
		std::filesystem::create_directory(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		l::DiskStoreOptions options;
		options.mode = l::StoreOpenMode::FreshEmpty;
		options.limits = {1024 * 1024, 100, 10};
		{
			auto domain = runtime.domain(account, 1);
			auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
			drive(store);
			store->begin(*first);
			drive(store);
			store->begin(*second, first->descriptor().cursor);
			drive(store);
			store->close();
			store.reset();
			domain.close().wait();
		}
		const auto name = l::generation_name(first->descriptor().generation);
		const auto lease = directory / (name + ".lease");
		const auto image = directory / (name + ".image");
		const auto temporary = directory / (name + ".descriptor");
		if (scenario >= 1 && scenario <= 3) {
			std::array<char, 128> raw;
			std::ifstream stream(lease, std::ios::binary);
			stream.read(raw.data(), raw.size());
			check(static_cast<bool>(stream));
			auto proof = l::decode_generation_ownership(raw);
			if (scenario == 1)
				proof.storage[0] ^= 1;
			if (scenario == 2)
				proof.generation[0] ^= 1;
			raw = l::encode_generation_ownership(proof);
			if (scenario == 3)
				raw[11] = 2;
			write_file(lease, raw);
		} else if (scenario == 4)
			std::filesystem::resize_file(image, first->descriptor().length + 1);
		else if (scenario == 5) {
			std::filesystem::remove(image);
			std::filesystem::create_symlink("owner.lock", image);
		} else if (scenario == 6)
			std::filesystem::create_hard_link(image, directory / "foreign-image-alias");
		else if (scenario == 7 || scenario == 8 || scenario == 15 || scenario == 16) {
			std::string bytes(first->bytes()->view());
			if (scenario == 7)
				bytes[7] = '2';
			if (scenario == 8)
				bytes[171] = 2;
			if (scenario == 15) {
				bytes.resize(170);
				bytes[168] = 2;
			}
			if (scenario == 16) {
				bytes.resize(179);
				bytes[176] = 2;
			}
			write_file(image, std::span<const char>(bytes));
		} else if (scenario >= 9 && scenario <= 13) {
			auto descriptor = first->descriptor();
			if (scenario == 9)
				descriptor.generation[0] ^= 1;
			if (scenario == 10)
				++descriptor.cursor.revision;
			auto record = l::encode_descriptor(descriptor);
			if (scenario == 12)
				record[16] ^= 1;
			if (scenario == 13)
				record[11] = 2;
			write_file(temporary, std::span<const char>(record).first(scenario >= 11 ? 100 : record.size()));
		} else if (scenario == 14)
			std::filesystem::resize_file(image, 100);
		else if (scenario == 17) {
			check(first->descriptor().length > second->descriptor().length);
			options.recovery_limits.bytes = second->descriptor().length;
		} else if (scenario >= 18 && scenario < 18 + image_offsets.size()) {
			const auto offset = image_offsets[scenario - 18];
			std::string bytes(first->bytes()->view().substr(0, offset + 1));
			bytes[offset] ^= static_cast<char>(0x80);
			write_file(image, std::span<const char>(bytes));
		} else if (scenario >= 18 + image_offsets.size() && scenario < prefix_scenarios) {
			const auto offset = descriptor_offsets[scenario - 18 - image_offsets.size()];
			auto record = l::encode_descriptor(first->descriptor());
			record[offset] ^= static_cast<char>(0x80);
			write_file(temporary, std::span<const char>(record).first(offset + 1));
			std::filesystem::remove(image); // No full header can supply descriptor scope.
		} else if (scenario >= prefix_scenarios) {
			const auto index = scenario - prefix_scenarios;
			const auto offset = identity_offsets[index];
			const auto length = index % 4 == 3 ? 32u : 16u;
			if (index < 4) {
				std::string bytes(first->bytes()->view().substr(0, offset + length));
				std::fill(bytes.begin() + offset, bytes.end(), '\0');
				write_file(image, std::span<const char>(bytes));
			} else {
				auto record = l::encode_descriptor(first->descriptor());
				std::fill(record.begin() + offset, record.begin() + offset + length, '\0');
				write_file(temporary, std::span<const char>(record).first(offset + length));
				std::filesystem::remove(image);
			}
		}
		options.mode = l::StoreOpenMode::ExistingManaged;
		auto domain = runtime.domain(account, 1);
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		check(store->status() == l::StoreStatus::Ready && store->stats().used == census(directory));
		const auto before = store->stats().used;
		store->request_collection();
		store->advance();
		drive(store);
		const bool eligible = scenario == 0 || scenario == 11 || scenario == 14 || scenario == 17;
		check(store->status() == l::StoreStatus::Ready && store->stats().used == census(directory));
		if (eligible)
			check(!std::filesystem::exists(lease) && !std::filesystem::exists(image) &&
				  !std::filesystem::exists(temporary) && store->stats().used.generations == 1);
		else
			check(store->stats().used == before && std::filesystem::exists(lease));
		check(store->selection().descriptor == second->descriptor());
		store->close();
		store.reset();
		domain.close().wait();
	}
	std::cout << "owned partial/quarantine families: " << scenarios << "\n";
}
void publication_cancellation(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
							  const std::filesystem::path &parent) {
	auto builder = build(account, 'z', 42);
	const auto baseline = account->used();
	std::size_t cases = 0;
	for (bool held : {false, true}) {
		for (std::size_t boundary = 0; boundary < 22; ++boundary) {
			check_usage(account, baseline);
			const auto directory = parent / ("cancel-" + std::to_string(cases++));
			std::filesystem::create_directory(directory);
			std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			auto domain = runtime.domain(account, 1);
			const auto domain_baseline = account->used();
			l::DiskStoreOptions options;
			options.mode = l::StoreOpenMode::FreshEmpty;
			options.limits = {1024 * 1024, 100, 10};
			auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
			drive(store);
			store->begin(*builder);
			auto job = store->operation();
			auto pressure = account->admit(account->limit() - account->used());
			std::size_t step = 0;
			while (!job->done()) {
				if (step == boundary && !held)
					store->cancel_publication();
				if (job->done())
					break;
				job->submitted();
				auto result = kronuz::journal::detail::execute_primitive(job->io(), job->request());
				if (step == boundary && held) {
					store->cancel_publication();
					c::Result stale;
					stale.token = result.token;
					++stale.token.operation;
					check(!job->complete(std::move(stale)) && job->in_flight());
				}
				check(job->complete(std::move(result)));
				++step;
			}
			check(store->status() == l::StoreStatus::Ready);
			check(store->publication_status() == l::DiskPublicationStatus::Cancelled ||
				  store->publication_status() == l::DiskPublicationStatus::Published);
			check(store->stats().used == census(directory) && store->stats().reserved == l::DiskUsage{});
			if (store->publication_status() == l::DiskPublicationStatus::Published)
				check(store->selection().descriptor == builder->descriptor());
			else
				check(store->selection().knowledge == l::SelectionKnowledge::Absent);
			job.reset();
			pressure.reset();
			store->request_collection();
			store->advance();
			drive(store);
			check(store->stats().used == census(directory));
			check(store->stats().used.generations ==
				  (store->publication_status() == l::DiskPublicationStatus::Published ? 1u : 0u));
			store->close();
			store.reset();
			domain.close().wait();
			check_usage(account, domain_baseline);
			check(domain_baseline >= baseline);
		}
	}
	std::cout << "publication cancellation boundaries: " << cases << "\n";
}
void startup_shutdown(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
					  const std::filesystem::path &parent) {
	std::size_t cases = 0;
	for (bool held : {false, true}) {
		for (std::size_t boundary = 0; boundary < 12; ++boundary) {
			const auto directory = parent / ("startup-close-" + std::to_string(cases++));
			std::filesystem::create_directory(directory);
			std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			auto domain = runtime.domain(account, 1);
			l::DiskStoreOptions options;
			options.mode = l::StoreOpenMode::FreshEmpty;
			options.limits = {1024 * 1024, 100, 10};
			auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
			std::size_t step = 0;
			while (auto job = store->operation()) {
				if (step == boundary && !held) {
					store->close();
					if (job->done()) {
						store->advance();
						break;
					}
				}
				job->submitted();
				auto result = kronuz::journal::detail::execute_primitive(job->io(), job->request());
				check(!result.error);
				if (step == boundary && held)
					store->close();
				check(job->complete(std::move(result)));
				store->advance();
				++step;
			}
			store->close();
			check(store->drained() && !store->error());
			store.reset();
			domain.close().wait();
		}
	}
	std::cout << "startup shutdown boundaries: " << cases << "\n";
}
void detached_native_publication(d::RetirementRuntime &runtime,
								 const std::shared_ptr<d::StorageLedger> &account,
								 const std::filesystem::path &parent) {
	const auto directory = parent / "native-detached";
	std::filesystem::create_directory(directory);
	std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
	auto domain = runtime.domain(account, 1);
	l::DiskStoreOptions options;
	options.mode = l::StoreOpenMode::FreshEmpty;
	options.limits = {1024 * 1024, 100, 10};
	auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
	drive(store);
	auto builder = build(account, 'n', 42);
	auto publisher = std::make_unique<l::ImagePublisher>(store);
	check(publisher->state() == l::PublicationState::Idle && !publisher->published_cursor());
	publisher->begin(*builder);
	for (unsigned i = 0; i < 3; ++i) {
		auto job = publisher->operation();
		check(job->request().kind == c::Kind::InspectEntry);
		job->submitted();
		check(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
	}
	c::AllocationContext context(d::make_storage_resource(account));
	auto queue = c::make_native_queue(context);
	auto job = publisher->operation();
	check(job->request().kind == c::Kind::CreateInto);
	check(queue->submit(job));
	publisher->close();
	check(!store->drained());
	publisher.reset(); // The worker coordinator retains the store through drainage.
	job.reset();
	auto pressure = account->admit(account->limit() - account->used());
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	for (;;) {
		auto original = queue->poll();
		if (!original) {
			check(std::chrono::steady_clock::now() < deadline);
			std::this_thread::yield();
			continue;
		}
		job = original->operation;
		check(!original->completion.error);
		check(job->complete(std::move(original->completion)));
		if (job->done())
			break;
		check(queue->submit(job));
		job.reset();
	}
	check(census(directory) == l::DiskUsage{64 + 128, 3, 1});
	check(!std::filesystem::exists(directory /
								   (l::generation_name(builder->descriptor().generation) + ".image")));
	check(store->stats().used == census(directory) && store->stats().reserved == l::DiskUsage{});
	store->advance();
	check(store->drained());
	store.reset();
	job.reset();
	queue.reset(); // Joins only on this explicit shutdown caller.
	pressure.reset();
	domain.close().wait();
	check(domain.residents() == 0);
}
void publication_faults(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
						const std::filesystem::path &parent) {
	auto builder = build(account, 'f', 42);
	const auto injected = std::make_exception_ptr(std::runtime_error("injected managed original failure"));
	std::size_t cases = 0;
	for (bool after : {false, true}) {
		for (std::size_t boundary = 0; boundary < 20; ++boundary) {
			const auto directory = parent / ("fault-" + std::to_string(cases++));
			std::filesystem::create_directory(directory);
			std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			l::DiskStoreOptions options;
			options.mode = l::StoreOpenMode::FreshEmpty;
			options.limits = {1024 * 1024, 100, 10};
			{
				auto domain = runtime.domain(account, 1);
				auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
				drive(store);
				store->begin(*builder);
				auto job = store->operation();
				std::size_t step = 0;
				bool fired = false;
				while (!job->done()) {
					const auto &request = job->request();
					job->submitted();
					c::Result result;
					result.token = request.token;
					const bool fault = step++ == boundary;
					if (!fault || after || request.kind == c::Kind::CloseFile)
						result = kronuz::journal::detail::execute_primitive(job->io(), request);
					if (fault) {
						check(!result.error);
						// Valid closed CreateInto destinations remain closed on failure,
						// even when exclusive creation left a namespace artifact.
						if (after && request.kind == c::Kind::CreateInto)
							request.file->close();
						result.error = injected;
						fired = true;
					}
					check(job->complete(std::move(result)));
				}
				check(fired && store->status() == l::StoreStatus::Fenced);
				check(store->publication_status() == l::DiskPublicationStatus::Fenced);
				const auto stats = store->stats();
				const auto actual = census(directory);
				check(stats.fenced && stats.reserved != l::DiskUsage{});
				check(stats.used.bytes + stats.reserved.bytes >= actual.bytes);
				check(stats.used.entries + stats.reserved.entries >= actual.entries);
				job.reset();
				store->close();
				check(store->drained());
				store.reset();
				domain.close().wait();
			}
			options.mode = l::StoreOpenMode::ExistingManaged;
			{
				auto domain = runtime.domain(account, 1);
				auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
				drive(store);
				check(store->status() == l::StoreStatus::Ready && store->stats().used == census(directory));
				store->close();
				store.reset();
				domain.close().wait();
			}
		}
	}
	std::cout << "publication original fault boundaries: " << cases << "\n";
}
int crash_child(std::string_view profile, const std::filesystem::path &directory, c::Kind target,
				std::size_t occurrence) {
	auto baseline = std::make_shared<d::StorageLedger>(1024 * 1024);
	auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
	d::RetirementRuntime runtime(baseline, 2);
	auto domain = runtime.domain(account, 1);
	l::DiskStoreOptions options;
	options.mode = l::StoreOpenMode::FreshEmpty;
	options.limits = {1024 * 1024, 100, 10};
	auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
	if (profile != "startup")
		drive(store);
	if (profile == "publication") {
		auto builder = build(account, 'p', 42);
		store->begin(*builder);
	} else if (profile == "gc") {
		for (unsigned i = 0; i < 2; ++i) {
			auto builder = build(account, static_cast<char>('a' + i), 42 + i);
			const auto selected = store->selection().descriptor;
			store->begin(*builder, selected ? std::optional(selected->cursor) : std::nullopt);
			drive(store);
		}
		store->request_collection();
		store->advance();
	}
	while (auto job = store->operation()) {
		const auto kind = job->request().kind;
		job->submitted();
		auto result = kronuz::journal::detail::execute_primitive(job->io(), job->request());
		check(!result.error);
		if (kind == target && occurrence-- == 0)
			::raise(SIGKILL);
		check(job->complete(std::move(result)));
		store->advance();
	}
	store->close();
	store.reset();
	domain.close().wait();
	return 2; // The requested crash boundary must exist.
}
void crash_recovery(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
					const std::filesystem::path &parent, const std::filesystem::path &executable) {
	struct Boundary {
		std::string_view profile;
		c::Kind kind;
		std::size_t occurrence;
	};
	const std::array boundaries{Boundary{"startup", c::Kind::ScanInto, 0},
								Boundary{"startup", c::Kind::CreateInto, 0},
								Boundary{"startup", c::Kind::Write, 0},
								Boundary{"startup", c::Kind::Sync, 0},
								Boundary{"startup", c::Kind::DirectorySync, 0},
								Boundary{"publication", c::Kind::CreateInto, 0},
								Boundary{"publication", c::Kind::Write, 0},
								Boundary{"publication", c::Kind::CreateInto, 1},
								Boundary{"publication", c::Kind::Write, 1},
								Boundary{"publication", c::Kind::CreateInto, 2},
								Boundary{"publication", c::Kind::Write, 2},
								Boundary{"publication", c::Kind::Replace, 0},
								Boundary{"publication", c::Kind::DirectorySync, 3},
								Boundary{"gc", c::Kind::Remove, 0},
								Boundary{"gc", c::Kind::DirectorySync, 0},
								Boundary{"gc", c::Kind::Remove, 1},
								Boundary{"gc", c::Kind::DirectorySync, 1},
								Boundary{"gc", c::Kind::CloseFile, 1}};
	std::size_t index = 0;
	for (const auto boundary : boundaries) {
		const auto directory = parent / ("crash-" + std::to_string(index++));
		std::filesystem::create_directory(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		std::array arguments{executable.string(),
							 std::string("--crash"),
							 std::string(boundary.profile),
							 directory.string(),
							 std::to_string(static_cast<unsigned>(boundary.kind)),
							 std::to_string(boundary.occurrence)};
		std::array<char *, 7> argv{};
		for (std::size_t i = 0; i < arguments.size(); ++i)
			argv[i] = arguments[i].data();
		pid_t child = -1;
		check(::posix_spawn(&child, executable.c_str(), nullptr, nullptr, argv.data(), environ) == 0);
		int status = 0;
		check(::waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
		auto domain = runtime.domain(account, 1);
		l::DiskStoreOptions options;
		options.mode = std::filesystem::exists(directory / "storage.meta") ? l::StoreOpenMode::ExistingManaged
																		   : l::StoreOpenMode::UpgradeLegacy;
		options.limits = {1024 * 1024, 100, 10};
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		if (boundary.profile == "startup" && boundary.kind == c::Kind::CreateInto) {
			check(store->status() == l::StoreStatus::Fenced);
			check(std::filesystem::file_size(directory / "storage.meta") == 0);
		} else {
			check(store->status() == l::StoreStatus::Ready && store->stats().used == census(directory));
			store->request_collection();
			store->advance();
			drive(store);
			check(store->stats().used == census(directory));
		}
		store->close();
		store.reset();
		domain.close().wait();
	}
	std::cout << "real SIGKILL recovery boundaries: " << boundaries.size() << "\n";
}
void batch_reclamation(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
					   const std::filesystem::path &parent, std::size_t foreign_count) {
	const auto directory = parent / "batch";
	std::filesystem::create_directory(directory);
	std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
	l::DiskStoreOptions options;
	options.mode = l::StoreOpenMode::FreshEmpty;
	options.limits = {1024 * 1024, foreign_count + 100, 100};
	options.gc_scan_slice = foreign_count ? 256 : 1000;
	{
		auto domain = runtime.domain(account, 1);
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		drive(store);
		for (unsigned i = 0; i < 9; ++i) {
			auto builder = build(account, static_cast<char>('a' + i), 42 + i);
			const auto selected = store->selection().descriptor;
			store->begin(*builder, selected ? std::optional(selected->cursor) : std::nullopt);
			drive(store);
		}
		check(store->stats().used == census(directory));
		store->close();
		store.reset();
		domain.close().wait();
	}
	for (std::size_t i = 0; i < foreign_count; ++i) {
		const auto name = directory / ("foreign-" + std::to_string(i));
		const int fd = ::open(name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		check(fd >= 0 && ::close(fd) == 0);
	}
	options.mode = l::StoreOpenMode::ExistingManaged;
	auto domain = runtime.domain(account, 1);
	auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
	drive(store);
	check(store->status() == l::StoreStatus::Ready && store->stats().used == census(directory));
	const auto resident = account->used();
	const auto before = store->stats().used;
	auto pressure = account->admit(account->limit() - resident);
	std::array<c::File *, 8> held{};
	std::size_t held_count = 0, peak = 0, scans = 0, inspections = 0, barriers = 0, read_bytes = 0;
	const auto wall_start = std::chrono::steady_clock::now();
	const auto cpu_start = std::clock();
	store->request_collection();
	store->advance();
	while (auto job = store->operation()) {
		const auto &request = job->request();
		const auto kind = request.kind;
		auto *file = request.file.get();
		job->submitted();
		auto result = kronuz::journal::detail::execute_primitive(job->io(), request);
		check(!result.error);
		if (kind == c::Kind::ExclusiveLease && result.count) {
			check(held_count < held.size());
			held[held_count++] = file;
			peak = std::max(peak, held_count);
		}
		if (kind == c::Kind::CloseFile) {
			for (std::size_t i = 0; i < held_count; ++i) {
				if (held[i] == file) {
					held[i] = held[--held_count];
					break;
				}
			}
		}
		if (kind == c::Kind::ScanInto)
			++scans;
		if (kind == c::Kind::InspectEntry)
			++inspections;
		if (kind == c::Kind::DirectorySync)
			++barriers;
		if (kind == c::Kind::Read)
			read_bytes += result.count;
		check(job->complete(std::move(result)));
		store->advance();
		check(account->used() == account->limit());
	}
	const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();
	const auto cpu = double(std::clock() - cpu_start) / CLOCKS_PER_SEC;
	check(store->status() == l::StoreStatus::Ready && store->stats().used == census(directory));
	check(store->stats().used.generations == 1 && store->stats().used.entries == foreign_count + 5);
	check(scans == 1 && held_count == 0 && peak <= 8 && read_bytes == 8 * (128 + 192));
	if (!foreign_count)
		check(peak == 8 && barriers == 2);
	pressure.reset();
	check(account->used() == resident);
	std::cout << "GC real directory: foreign=" << foreign_count << " resident=" << resident
			  << " scans=" << scans << " inspections=" << inspections << " barriers=" << barriers
			  << " peak_leases=" << peak << " read_bytes=" << read_bytes
			  << " reclaimed_bytes=" << before.bytes - store->stats().used.bytes << " wall_seconds=" << wall
			  << " cpu_seconds=" << cpu << '\n';
	store->close();
	store.reset();
	domain.close().wait();
}
void reclaim_boundaries(d::RetirementRuntime &runtime, const std::shared_ptr<d::StorageLedger> &account,
						const std::filesystem::path &parent) {
	auto first = build(account, 'a', 42);
	auto second = build(account, 'b', 43);
	const auto injected =
		std::make_exception_ptr(std::runtime_error("injected reclamation original failure"));
	std::size_t normal_steps = 0, cases = 0;
	// Reference, cancellation before/after acceptance, and failures before/after effects.
	for (unsigned mode = 0; mode < 5; ++mode) {
		const auto count = mode ? normal_steps : 1;
		for (std::size_t boundary = 0; boundary < count; ++boundary) {
			const auto directory = parent / ("gc-boundary-" + std::to_string(cases++));
			std::filesystem::create_directory(directory);
			std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			auto domain = runtime.domain(account, 1);
			l::DiskStoreOptions options;
			options.mode = l::StoreOpenMode::FreshEmpty;
			options.limits = {1024 * 1024, 100, 10};
			auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
			drive(store);
			store->begin(*first);
			drive(store);
			store->begin(*second, first->descriptor().cursor);
			drive(store);
			const auto initial = store->stats().used;
			store->request_collection();
			store->advance();
			auto job = store->operation();
			std::size_t step = 0;
			bool fired = false;
			while (!job->done()) {
				if (mode == 1 && step == boundary) {
					store->close();
					fired = true;
				}
				if (job->done())
					break;
				const auto &request = job->request();
				const auto kind = request.kind;
				job->submitted();
				c::Result result;
				result.token = request.token;
				const bool fault = mode >= 3 && step == boundary;
				if (!fault || mode == 4 || kind == c::Kind::CloseFile || kind == c::Kind::CloseCursor)
					result = kronuz::journal::detail::execute_primitive(job->io(), request);
				if (mode == 2 && step == boundary) {
					store->close();
					fired = true;
				}
				if (fault) {
					check(!result.error);
					if (mode == 4 && kind == c::Kind::OpenCandidateInto && result.count)
						request.file->close();
					if (mode == 4 && kind == c::Kind::OpenInto)
						request.file->close();
					if (mode == 4 && kind == c::Kind::ScanInto)
						request.cursor->close();
					result.error = injected;
					fired = true;
				}
				check(job->complete(std::move(result)));
				++step;
				if (!job->done() && !store->operation())
					store->advance();
			}
			if (!mode)
				normal_steps = step;
			else
				check(fired);
			if (mode >= 3) {
				check(store->status() == l::StoreStatus::Fenced && store->stats().fenced);
				const auto retained = store->stats().used;
				const auto actual = census(directory);
				check(retained.bytes <= initial.bytes && retained.bytes >= actual.bytes);
				check(retained.entries <= initial.entries && retained.entries >= actual.entries);
				check(retained.generations <= initial.generations &&
					  retained.generations >= actual.generations);
			} else
				check(store->stats().used == census(directory));
			check(store->selection().descriptor == second->descriptor());
			job.reset();
			store->close();
			check(store->drained());
			store.reset();
			domain.close().wait();
			if (mode >= 3) {
				auto reopened_domain = runtime.domain(account, 1);
				options.mode = l::StoreOpenMode::ExistingManaged;
				auto reopened =
					l::make_disk_store(reopened_domain, directory, std::make_shared<int>(1), options);
				drive(reopened);
				check(reopened->status() == l::StoreStatus::Ready &&
					  reopened->stats().used == census(directory));
				reopened->request_collection();
				reopened->advance();
				drive(reopened);
				check(reopened->stats().used == census(directory) && reopened->stats().used.generations == 1);
				reopened->close();
				reopened.reset();
				reopened_domain.close().wait();
			}
		}
	}
	std::cout << "GC cancellation/fault boundaries: " << cases - 1 << " (" << normal_steps << " originals)\n";
}
} // namespace
int main(int argc, char **argv) {
	try {
		if (argc == 6 && std::string_view(argv[1]) == "--crash")
			return crash_child(argv[2], argv[3], static_cast<c::Kind>(std::stoul(argv[4])),
							   std::stoull(argv[5]));
		auto root =
			std::filesystem::current_path() / ".scratch" / ("disk-store-" + std::to_string(::getpid()));
		std::filesystem::create_directories(root);
		std::filesystem::permissions(root, std::filesystem::perms::owner_all);
		auto baseline = std::make_shared<d::StorageLedger>(1024 * 1024);
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		d::RetirementRuntime runtime(baseline, 2);
		l::DiskStoreOptions options;
		options.mode = l::StoreOpenMode::FreshEmpty;
		options.limits = {64, 2, 0};
		for (unsigned dimension = 0; dimension < 2; ++dimension) {
			auto domain = runtime.domain(account, 1);
			auto invalid = options;
			if (dimension == 0)
				--invalid.limits.bytes;
			else
				--invalid.limits.entries;
			bool rejected = false;
			try {
				l::make_disk_store(domain, root, std::make_shared<int>(1), invalid);
			} catch (const l::DiskPressure &) {
				rejected = true;
			}
			check(rejected && !std::filesystem::exists(root / "owner.lock"));
			domain.close().wait();
		}
		{
			auto domain = runtime.domain(account, 1);
			auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
			check(store->status() == l::StoreStatus::Starting);
			check(!store->stats().initialized && store->stats().reserved == l::DiskUsage{64, 2, 0});
			auto pressure = account->admit(account->limit() - account->used());
			drive(store);
			check(store->status() == l::StoreStatus::Ready && !store->error());
			check(store->stats().used == l::DiskUsage{64, 2, 0} && store->stats().reserved == l::DiskUsage{});
			check(store->selection().knowledge == l::SelectionKnowledge::Absent);
			check(std::filesystem::file_size(root / "storage.meta") == 64);
			store->close();
			check(store->drained());
			store.reset();
			domain.close().wait();
			pressure.reset();
		}
		check_released(account);
		options.mode = l::StoreOpenMode::ExistingManaged;
		{
			auto domain = runtime.domain(account, 1);
			auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
			auto pressure = account->admit(account->limit() - account->used());
			drive(store);
			check(store->status() == l::StoreStatus::Ready);
			check(store->stats().used == l::DiskUsage{64, 2, 0});
			store->close();
			store.reset();
			domain.close().wait();
			pressure.reset();
		}
		check_released(account);
		options.limits = {1024 * 1024, 100, 10};
		{
			auto domain = runtime.domain(account, 1);
			auto store = l::make_disk_store(domain, root, std::make_shared<int>(1), options);
			drive(store);
			check(store->status() == l::StoreStatus::Ready);
			int pinned = -1;
			for (unsigned iteration = 0; iteration < 2; ++iteration) {
				auto builder = build(account, static_cast<char>('a' + iteration), 42 + iteration);
				const auto base = store->selection().descriptor;
				store->begin(*builder, base ? std::optional(base->cursor) : std::nullopt);
				auto pressure = account->admit(account->limit() - account->used());
				// Settle in the retained job without coordinator advancement.
				auto job = store->operation();
				while (!job->done()) {
					job->submitted();
					check(
						job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
				}
				check(store->publication_status() == l::DiskPublicationStatus::Published);
				check(store->selection().descriptor == builder->descriptor());
				check(store->stats().used == census(root));
				check(store->stats().reserved == l::DiskUsage{});
				if (!iteration) {
					const auto name = l::generation_name(builder->descriptor().generation) + ".lease";
					pinned = ::open((root / name).c_str(), O_RDONLY | O_CLOEXEC);
					check(pinned >= 0 && ::flock(pinned, LOCK_SH | LOCK_NB) == 0);
				}
			}
			const auto before = store->stats().used;
			{
				auto pressure = account->admit(account->limit() - account->used());
				store->request_collection();
				store->advance();
				drive(store);
				check(store->status() == l::StoreStatus::Ready && store->stats().used == before);
				check(::close(pinned) == 0);
				store->request_collection();
				store->advance();
				drive(store);
				check(store->status() == l::StoreStatus::Ready && store->stats().used == census(root));
				check(store->stats().used.generations == 1 && store->stats().used.entries == 5);
				const auto after = store->stats().used;
				store->request_collection();
				store->advance();
				drive(store);
				check(store->stats().used == after);
			}
			store->close();
			check(store->drained());
			store.reset();
			domain.close().wait();
		}
		check_released(account);
		publication_cancellation(runtime, account, root);
		check_released(account);
		startup_shutdown(runtime, account, root);
		check_released(account);
		startup_profiles(runtime, account, root);
		check_released(account);
		recovery_durability(runtime, account, root);
		check_released(account);
		facade_cancellation(runtime, account, root);
		check_released(account);
		publication_admission(runtime, account, root);
		check_released(account);
		quarantine_families(runtime, account, root);
		check_released(account);
		detached_native_publication(runtime, account, root);
		check_released(account);
		publication_faults(runtime, account, root);
		check_released(account);
		crash_recovery(runtime, account, root, std::filesystem::absolute(argv[0]));
		check_released(account);
		batch_reclamation(runtime, account, root,
						  argc == 3 && std::string_view(argv[1]) == "--scale" ? std::stoull(argv[2]) : 0);
		check_released(account);
		reclaim_boundaries(runtime, account, root);
		check_released(account);
		std::filesystem::remove_all(root);
		runtime.shutdown();
		std::cout << "managed startup bootstrap and allocation-refusal qualification passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
