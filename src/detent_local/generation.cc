#include "paged_limits.hh"
#include "detent_local/generation.hh"
#include "detent_local/ownership.hh"
#include "generation_mapping.hh"
#include "leased_paged_root.hh"
#include "paged_dependency_discovery.hh"
#include "paged_progress_view.hh"
#include "paged_warm_validator.hh"
#include "recovery_context.hh"
#include "resources.hh"
#include "segment_names.hh"
#include "warm_recovery_eligibility.hh"
#include "warm_registry_merge.hh"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sha256.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace detent::local {
namespace {
constexpr std::string_view magic = "DTSELECT";
struct RecoverySelectionChanged : std::runtime_error {
	RecoverySelectionChanged() : std::runtime_error("managed recovery selection changed") {}
};
void require(bool condition) {
	if (!condition)
		throw std::runtime_error("invalid local generation descriptor");
}
using File = detail::RecoveryFile;
using Directory = detail::GenerationDirectory;

using Mapping = detail::GenerationMapping;

std::size_t mapping_capacity(std::uint64_t length) {
	const auto page = ::sysconf(_SC_PAGESIZE);
	require(page > 0);
	const auto bytes = static_cast<std::size_t>(page);
	require(length <= std::numeric_limits<std::size_t>::max() - (bytes - 1));
	return ((static_cast<std::size_t>(length) + bytes - 1) / bytes) * bytes;
}
void trusted_file(int fd, uid_t owner, std::uint64_t length) {
	struct stat info{};
	require(fd >= 0 && ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == owner &&
			info.st_nlink == 1 && !(info.st_mode & 0022) && info.st_size >= 0 &&
			std::uint64_t(info.st_size) == length);
}
void read_record(int fd, std::span<char> bytes) {
	std::size_t offset = 0;
	while (offset < bytes.size()) {
		auto count = ::pread(fd, bytes.data() + offset, bytes.size() - offset, offset);
		if (count < 0 && errno == EINTR)
			continue;
		require(count > 0);
		offset += static_cast<std::size_t>(count);
	}
}
} // namespace
std::array<char, descriptor_bytes> encode_descriptor(const Descriptor &d) {
	require(std::any_of(d.generation.begin(), d.generation.end(), [](char c) { return c != 0; }) &&
			d.length >= header_bytes);
	// Reuse the header codec to enforce identity and semantic profile invariants.
	Header h;
	h.cursor = d.cursor;
	h.profile = d.profile;
	encode_header(h);
	std::array<char, descriptor_bytes> bytes{};
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, 1, 4);
	write_integer(bytes, 12, descriptor_bytes, 4);
	std::copy(d.generation.begin(), d.generation.end(), bytes.begin() + 16);
	write_integer(bytes, 32, d.length, 8);
	std::copy(d.checksum.begin(), d.checksum.end(), bytes.begin() + 40);
	std::copy(d.cursor.cluster.begin(), d.cursor.cluster.end(), bytes.begin() + 72);
	std::copy(d.cursor.group.begin(), d.cursor.group.end(), bytes.begin() + 88);
	std::copy(d.cursor.restore_epoch.begin(), d.cursor.restore_epoch.end(), bytes.begin() + 104);
	std::copy(d.cursor.scope.begin(), d.cursor.scope.end(), bytes.begin() + 120);
	write_integer(bytes, 152, d.cursor.revision, 8);
	write_integer(bytes, 160, d.profile.semantic_id, 4);
	write_integer(bytes, 168, d.profile.required_capabilities, 8);
	return bytes;
}
Descriptor decode_descriptor(std::span<const char> bytes) {
	require(bytes.size() == descriptor_bytes && std::equal(magic.begin(), magic.end(), bytes.begin()) &&
			read_integer(bytes, 8, 4) == 1 && read_integer(bytes, 12, 4) == descriptor_bytes &&
			read_integer(bytes, 164, 4) == 0);
	Descriptor d;
	std::copy_n(bytes.begin() + 16, 16, d.generation.begin());
	d.length = read_integer(bytes, 32, 8);
	std::copy_n(bytes.begin() + 40, 32, d.checksum.begin());
	std::copy_n(bytes.begin() + 72, 16, d.cursor.cluster.begin());
	std::copy_n(bytes.begin() + 88, 16, d.cursor.group.begin());
	std::copy_n(bytes.begin() + 104, 16, d.cursor.restore_epoch.begin());
	std::copy_n(bytes.begin() + 120, 32, d.cursor.scope.begin());
	d.cursor.revision = read_integer(bytes, 152, 8);
	d.profile.semantic_id = read_integer(bytes, 160, 4);
	d.profile.required_capabilities = read_integer(bytes, 168, 8);
	encode_descriptor(d);
	return d;
}
std::string generation_name(const GenerationId &id) {
	std::array<char, 43> bytes;
	return std::string(generation_name(id, bytes));
}
std::string_view generation_name(const GenerationId &id, std::span<char> destination) {
	constexpr std::string_view prefix = "generation-";
	constexpr char hex[] = "0123456789abcdef";
	constexpr auto length = prefix.size() + std::tuple_size_v<GenerationId> * 2;
	if (destination.size() < length)
		throw std::length_error("generation name destination too small");
	std::copy(prefix.begin(), prefix.end(), destination.begin());
	auto offset = prefix.size();
	for (unsigned char c : id) {
		destination[offset++] = hex[c >> 4];
		destination[offset++] = hex[c & 15];
	}
	return {destination.data(), length};
}
GenerationSelection read_generation_selection(const std::filesystem::path &directory) {
	File dir(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
	struct stat info{};
	require(dir.fd >= 0 && ::fstat(dir.fd, &info) == 0 && S_ISDIR(info.st_mode) && !(info.st_mode & 0022));
	File selected(::openat(dir.fd, "selected", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
	trusted_file(selected.fd, info.st_uid, descriptor_bytes);
	std::array<char, descriptor_bytes> raw{};
	read_record(selected.fd, raw);
	return {decode_descriptor(raw), {std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)}};
}
RecoveredGeneration detail::RecoveryEngine::recover_routed(const std::filesystem::path &directory,
														   RecoveryContext legacy,
														   RecoveryContext paged_context,
														   std::optional<distribution::Cursor> expected) {
	if (paged_context.admission)
		return recover(directory, std::move(paged_context), expected);
	for (unsigned attempt = 0; attempt < 3; ++attempt) {
		File dir(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
		struct stat info{};
		require(dir.fd >= 0 && ::fstat(dir.fd, &info) == 0 && S_ISDIR(info.st_mode) &&
				!(info.st_mode & 0022));
		File selected(::openat(dir.fd, "selected", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
		trusted_file(selected.fd, info.st_uid, descriptor_bytes);
		std::array<char, descriptor_bytes> bytes;
		read_record(selected.fd, bytes);
		const auto descriptor = decode_descriptor(bytes);
		const auto name = generation_name(descriptor.generation) + ".lease";
		File lease(::openat(dir.fd, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
		if (lease.fd < 0 && errno == ENOENT)
			continue;
		require(lease.fd >= 0);
		if (::flock(lease.fd, LOCK_SH | LOCK_NB) != 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				continue;
			require(false);
		}
		struct stat held{}, named{};
		require(::fstat(lease.fd, &held) == 0);
		if (!held.st_nlink)
			continue;
		require(held.st_size == 0 || held.st_size == generation_ownership_bytes);
		trusted_file(lease.fd, info.st_uid, held.st_size);
		if (::fstatat(dir.fd, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0) {
			if (errno == ENOENT)
				continue;
			require(false);
		}
		if (held.st_dev != named.st_dev || held.st_ino != named.st_ino)
			continue;
		unsigned version = 1;
		if (held.st_size) {
			std::array<char, generation_ownership_bytes> record;
			read_record(lease.fd, record);
			version = decode_generation_ownership(record).layout_version;
		}
		auto context = version == 2 ? paged_context : legacy;
		context.selection =
			GenerationSelection{descriptor, {std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)}};
		context.layout_version = version;
		try {
			return recover(directory, std::move(context), expected);
		} catch (const RecoverySelectionChanged &) {
		}
	}
	throw RecoverySelectionChanged();
}
RecoveredGeneration recover_generation(const std::filesystem::path &directory, ViewLimits limits,
									   std::optional<distribution::Cursor> expected) {
	return recover_generation(directory, limits, expected, nullptr);
}
RecoveredGeneration recover_generation(const std::filesystem::path &directory, ViewLimits limits,
									   std::optional<distribution::Cursor> expected,
									   const RecoveredGeneration *prior) {
	auto &domain = resources::domain(limits);
	detail::RecoveryContext context{domain.factory(),
									domain.storage_account(),
									resources::mapping_account(limits),
									limits,
									prior ? prior->paged_
										  : distribution::RetirementHandle<detail::LeasedPagedRoot>{},
									{}};
	return detail::RecoveryEngine::recover(directory, std::move(context), expected);
}
RecoveredGeneration detail::RecoveryEngine::recover(const std::filesystem::path &directory,
													RecoveryContext context,
													std::optional<distribution::Cursor> expected) {
	bool claimed = false;
	try {
		auto limits = context.limits;
		limits.retirement_domain = nullptr;
		limits.mapping_account = context.mappings;
		if (!context.heap || !context.mappings || !context.factory.uses_account(context.heap) ||
			!context.budget.bytes || !context.budget.rows)
			throw std::invalid_argument("recovery context requires exact ledgers");
		auto factory = context.factory;
		const auto &prior = context.prior;
		const auto sample = [&context] {
			if (context.metrics) {
				context.metrics->sampled_heap_peak =
					std::max(context.metrics->sampled_heap_peak, std::uint64_t(context.heap->used()));
				context.metrics->sampled_mapping_peak =
					std::max(context.metrics->sampled_mapping_peak, std::uint64_t(context.mappings->used()));
			}
		};
		sample();
		if (context.admission) {
			if (auto failure = context.admission->failure())
				std::rethrow_exception(failure);
			context.admission->bind_resources(context.heap, context.mappings, prior);
			const auto selection = read_generation_selection(directory);
			context.admission->bind_selection(selection.descriptor, selection.directory_identity);
		}
		std::shared_ptr<File> dir;
		if (context.admission) {
			context.admission->claim_attempt();
			claimed = true;
			dir = context.admission->take_directory();
		} else {
			auto directory_owner = factory.make<Directory>();
			using DirectoryLease = distribution::RetirementHandle<Directory>;
			auto directory_lease = std::allocate_shared<DirectoryLease>(
				distribution::StorageAllocator<DirectoryLease>(context.heap), directory_owner);
			dir = {directory_lease, &directory_owner->file};
		}
		dir->fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		struct stat directory_info{};
		require(dir->fd >= 0 && ::fstat(dir->fd, &directory_info) == 0 && S_ISDIR(directory_info.st_mode) &&
				!(directory_info.st_mode & 0022));
		for (unsigned attempt = 0; attempt < (context.admission ? 1u : 3u); ++attempt) {
			File selected(::openat(dir->fd, "selected", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
			trusted_file(selected.fd, directory_info.st_uid, descriptor_bytes);
			std::array<char, descriptor_bytes> raw{};
			read_record(selected.fd, raw);
			auto descriptor = decode_descriptor(raw);
			if (context.selection && (context.selection->descriptor != descriptor ||
									  context.selection->directory_identity !=
										  DirectoryIdentity{std::uint64_t(directory_info.st_dev),
															std::uint64_t(directory_info.st_ino)}))
				throw RecoverySelectionChanged();
			require(descriptor.length <= limits.bytes);
			if (context.admission)
				context.admission->bind_selection(
					descriptor, {std::uint64_t(directory_info.st_dev), std::uint64_t(directory_info.st_ino)});
			auto name = generation_name(descriptor.generation);
			const auto mapping_account = context.mappings;
			auto mapping =
				context.admission ? context.admission->take_root() : factory.make<Mapping>(mapping_account);
			if (context.admission)
				require(mapping->account == mapping_account && mapping->length == descriptor.length);
			mapping->lease = ::openat(dir->fd, (name + ".lease").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
			if (mapping->lease < 0 && errno == ENOENT)
				continue;
			require(mapping->lease >= 0);
			if (::flock(mapping->lease, LOCK_SH | LOCK_NB) != 0) {
				if (errno == EWOULDBLOCK || errno == EAGAIN)
					continue;
				require(false);
			}
			struct stat lease_info{};
			require(::fstat(mapping->lease, &lease_info) == 0);
			if (lease_info.st_nlink == 0)
				continue;
			require(lease_info.st_size == 0 || lease_info.st_size == generation_ownership_bytes);
			trusted_file(mapping->lease, directory_info.st_uid, lease_info.st_size);
			// A collector may have unlinked an anchor between open and locking.
			// Match the held inode against the name before opening its image.
			struct stat named_lease{};
			if (::fstatat(dir->fd, (name + ".lease").c_str(), &named_lease, AT_SYMLINK_NOFOLLOW) != 0) {
				if (errno == ENOENT)
					continue;
				require(false);
			}
			if (named_lease.st_dev != lease_info.st_dev || named_lease.st_ino != lease_info.st_ino)
				continue;
			std::optional<GenerationOwnership> generation_ownership;
			if (lease_info.st_size == generation_ownership_bytes) {
				std::array<char, generation_ownership_bytes> lease_record;
				read_record(mapping->lease, lease_record);
				auto ownership = decode_generation_ownership(lease_record);
				generation_ownership = ownership;
				File storage(::openat(dir->fd, "storage.meta", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
				trusted_file(storage.fd, directory_info.st_uid, storage_identity_bytes);
				std::array<char, storage_identity_bytes> storage_record;
				read_record(storage.fd, storage_record);
				require(ownership.storage == decode_storage_identity(storage_record) &&
						ownership.generation == descriptor.generation &&
						ownership.image_length == descriptor.length &&
						ownership.image_checksum == descriptor.checksum);
			}
			if (context.layout_version &&
				*context.layout_version != (generation_ownership ? generation_ownership->layout_version : 1))
				throw RecoverySelectionChanged();
			mapping->image = ::openat(dir->fd, (name + ".image").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
			trusted_file(mapping->image, directory_info.st_uid, descriptor.length);
			mapping->length = descriptor.length;
			if (!context.admission)
				mapping->ticket = mapping->account->admit(mapping_capacity(descriptor.length));
			mapping->address = ::mmap(nullptr, mapping->length, PROT_READ, MAP_SHARED, mapping->image, 0);
			require(mapping->address != MAP_FAILED);
			if (context.metrics)
				++context.metrics->root_mapping_installs;
			auto bytes = std::span(static_cast<const char *>(mapping->address), mapping->length);
			SHA256 hash;
			for (std::size_t at = 0; at < bytes.size();) {
				auto count = std::min<std::size_t>(65536, bytes.size() - at);
				hash.add(bytes.data() + at, count);
				if (context.metrics)
					context.metrics->root_hash_bytes += count;
				at += count;
			}
			unsigned char digest[32];
			hash.getHash(digest);
			require(std::memcmp(digest, descriptor.checksum.data(), 32) == 0);
			if (generation_ownership && generation_ownership->layout_version == 2) {
				const auto bounds = detail::paged_limits(limits, descriptor.length);
				const auto root = paged::decode_root(bytes, bounds);
				require(root.cursor == descriptor.cursor && root.profile == descriptor.profile &&
						(!expected || (expected->same_identity(root.cursor) &&
									   root.cursor.revision >= expected->revision)));
				const auto storage = generation_ownership->storage;
				auto account = context.heap;
				const DirectoryIdentity identity{std::uint64_t(directory_info.st_dev),
												 std::uint64_t(directory_info.st_ino)};
				if (context.admission)
					context.admission->bind(descriptor, identity, storage);
				const bool warm =
					detail::eligible_warm_recovery(prior, root, identity, storage, account, mapping_account);
				if (context.admission)
					require(context.admission->warm() == warm);
				const bool progress = warm && detail::reuses_paged_state(prior->root(), root);
				if (context.metrics) {
					context.metrics->warm = warm;
					context.metrics->progress = progress;
				}
				std::optional<detail::WarmGraphValidator> warm_validator;
				std::optional<detail::DependencyDiscovery> discovery;
				distribution::RetirementHandle<detail::SegmentRegistry> registry;
				if (progress) {
					registry = prior->registry();
					const auto counts = prior->counts();
					require(counts.strings <= limits.strings &&
							counts.maximum_overrides <= limits.overrides &&
							registry->physical_bytes() <= bounds.bytes);
				} else {
					discovery.emplace(
						account, factory, root, storage, bounds,
						warm ? prior->registry() : distribution::RetirementHandle<detail::SegmentRegistry>{},
						context.admission ? (warm ? context.admission->fresh_registry()
												  : context.admission->target_registry())
										  : distribution::RetirementHandle<detail::SegmentRegistry>{});
					while (!discovery->done()) {
						auto budget = context.budget;
						discovery->step(budget);
						sample();
						if (auto missing = discovery->missing()) {
							const detail::SegmentNames names(*missing);
							File record(
								::openat(dir->fd, names.ownership.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
							trusted_file(record.fd, directory_info.st_uid, segment_ownership_bytes);
							std::array<char, segment_ownership_bytes> record_bytes;
							read_record(record.fd, record_bytes);
							const auto ownership = decode_segment_ownership(record_bytes);
							require(ownership.storage == storage && ownership.segment == *missing);
							File payload(
								::openat(dir->fd, names.payload.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
							auto segment =
								context.admission
									? context.admission->take_segment(ownership)
									: factory.make<detail::SegmentMapping>(mapping_account, payload.fd,
																		   directory_info.st_uid, ownership);
							if (context.admission)
								segment->install(payload.fd, directory_info.st_uid);
							if (context.metrics)
								++context.metrics->segment_mapping_installs;
							sample();
							discovery->supply(std::move(segment));
						}
					}
					registry = discovery->take();
				}

				if (warm && !progress) {
					using Registry = distribution::RetirementHandle<detail::SegmentRegistry>;
					auto fresh = registry;
					std::shared_ptr<const void> owner =
						context.admission ? context.admission->fresh_owner()
										  : std::allocate_shared<Registry>(
												distribution::StorageAllocator<Registry>(account), fresh);
					warm_validator.emplace(account, prior, owner, fresh->inputs(), root, bounds,
										   detail::PagedSemanticLimits{limits.strings, limits.overrides});
					while (!warm_validator->done()) {
						auto budget = context.budget;
						warm_validator->step(budget);
						sample();
					}
					detail::WarmRegistryMerge merge(
						account, factory, prior, fresh, *warm_validator, bounds,
						context.admission ? context.admission->target_registry()
										  : distribution::RetirementHandle<detail::SegmentRegistry>{});
					while (!merge.done()) {
						auto budget = context.budget;
						merge.step(budget);
						sample();
					}
					registry = merge.take();
				}

				using RootLease = distribution::RetirementHandle<Mapping>;
				std::shared_ptr<const void> root_lease =
					context.admission ? context.admission->root_lease()
									  : std::allocate_shared<RootLease>(
											distribution::StorageAllocator<RootLease>(account), mapping);
				auto capability = context.admission
									  ? context.admission->capability()
									  : factory.make<detail::LeasedPagedRoot>(
											dir, root_lease, registry, descriptor, identity, storage, root);
				using Capability = distribution::RetirementHandle<detail::LeasedPagedRoot>;
				std::optional<detail::GraphValidator> validator;
				if (!warm) {
					std::shared_ptr<const void> owner =
						context.admission
							? context.admission->capability_owner()
							: std::allocate_shared<Capability>(
								  distribution::StorageAllocator<Capability>(account), capability);
					validator.emplace(account, owner, registry->inputs(), root, storage, bounds,
									  detail::PagedSemanticLimits{limits.strings, limits.overrides});
					while (!validator->done()) {
						auto budget = context.budget;
						validator->step(budget);
						sample();
					}
					for (;;) {
						auto budget = context.budget;
						if (validator->record_reachability(*registry, budget))
							break;
					}
				}
				for (std::size_t i = 0; i < registry->inputs().size(); ++i) {
					const auto &segment = registry->mapping(i);
					const detail::SegmentNames names(segment->ownership().segment);
					File record(::openat(dir->fd, names.ownership.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
					trusted_file(record.fd, directory_info.st_uid, segment_ownership_bytes);
					std::array<char, segment_ownership_bytes> record_bytes;
					read_record(record.fd, record_bytes);
					require(decode_segment_ownership(record_bytes) == segment->ownership());
					struct stat named{};
					require(::fstatat(dir->fd, names.payload.data(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
							S_ISREG(named.st_mode) && named.st_uid == directory_info.st_uid &&
							named.st_nlink == 1 && !(named.st_mode & 0022) && named.st_size >= 0 &&
							std::uint64_t(named.st_size) == segment->ownership().length &&
							detail::SegmentFileIdentity{std::uint64_t(named.st_dev),
														std::uint64_t(named.st_ino)} == segment->identity());
				}
				trusted_file(mapping->image, directory_info.st_uid, descriptor.length);
				trusted_file(mapping->lease, directory_info.st_uid, generation_ownership_bytes);
				struct stat held_image{}, named_image{}, named_anchor{};
				require(
					::fstat(mapping->image, &held_image) == 0 &&
					::fstatat(dir->fd, (name + ".image").c_str(), &named_image, AT_SYMLINK_NOFOLLOW) == 0 &&
					held_image.st_dev == named_image.st_dev && held_image.st_ino == named_image.st_ino &&
					::fstatat(dir->fd, (name + ".lease").c_str(), &named_anchor, AT_SYMLINK_NOFOLLOW) == 0 &&
					lease_info.st_dev == named_anchor.st_dev && lease_info.st_ino == named_anchor.st_ino);
				std::array<char, generation_ownership_bytes> final_anchor;
				read_record(mapping->lease, final_anchor);
				require(decode_generation_ownership(final_anchor) == *generation_ownership);
				File final_storage(::openat(dir->fd, "storage.meta", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
				trusted_file(final_storage.fd, directory_info.st_uid, storage_identity_bytes);
				std::array<char, storage_identity_bytes> final_identity;
				read_record(final_storage.fd, final_identity);
				require(decode_storage_identity(final_identity) == storage);
				if (progress)
					capability->seal_progress(*prior);
				else
					capability->seal(warm ? warm_validator->counts() : validator->counts());
				if (context.metrics) {
					if (discovery)
						context.metrics->segment_hash_bytes += discovery->hashed_segment_bytes();
					if (warm_validator) {
						context.metrics->segment_hash_bytes += warm_validator->hashed_segment_bytes();
						context.metrics->definitions_validated += warm_validator->definitions_validated();
						context.metrics->old_definition_headers += warm_validator->old_definition_headers();
					}
					if (validator) {
						context.metrics->segment_hash_bytes += validator->hashed_segment_bytes();
						context.metrics->definitions_validated += root.records;
					}
				}
				if (warm_validator)
					for (;;) {
						auto budget = context.budget;
						if (warm_validator->discard(budget))
							break;
					}
				if (discovery)
					for (;;) {
						auto budget = context.budget;
						if (discovery->discard(budget))
							break;
					}
				sample();
				auto view = context.admission ? context.admission->take_view()
											  : detail::PagedProgressView::take(factory, account, capability);
				auto result =
					RecoveredGeneration(std::move(view), descriptor, dir, identity, std::move(capability));
				if (context.admission)
					context.admission->complete_attempt();
				return result;
			}
			auto header = decode_header(bytes, limits.bytes);
			require(header.cursor == descriptor.cursor && header.profile == descriptor.profile);
			using Lease = distribution::RetirementHandle<Mapping>;
			auto lease =
				std::allocate_shared<Lease>(distribution::StorageAllocator<Lease>(context.heap), mapping);
			ImageValidator validator(lease, bytes, limits, expected, factory, context.heap);
			while (!validator.done()) {
				auto budget = context.budget;
				validator.step(budget);
			}
			return RecoveredGeneration(
				validator.take(), descriptor, dir,
				{std::uint64_t(directory_info.st_dev), std::uint64_t(directory_info.st_ino)});
		}
		throw std::runtime_error("local generation changed while acquiring its lease");
	} catch (...) {
		if (claimed)
			context.admission->fail_attempt(std::current_exception());
		throw;
	}
}

ReadView open_generation(const std::filesystem::path &directory, ViewLimits limits,
						 std::optional<distribution::Cursor> expected) {
	return recover_generation(directory, limits, expected).view();
}
} // namespace detent::local
