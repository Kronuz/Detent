#include "detent_local/generation.hh"
#include "detent_local/ownership.hh"
#include "leased_paged_root.hh"
#include "paged_dependency_discovery.hh"
#include "paged_progress_view.hh"
#include "resources.hh"
#include "segment_names.hh"
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
void require(bool condition) {
	if (!condition)
		throw std::runtime_error("invalid local generation descriptor");
}
struct File {
	int fd = -1;
	explicit File(int value) : fd(value) {}
	~File() {
		if (fd >= 0)
			::close(fd);
	}
	File(const File &) = delete;
};
struct Directory : distribution::RetirementNode {
	File file{-1};
};

struct Mapping : distribution::RetirementNode {
	std::shared_ptr<distribution::StorageLedger> account;
	distribution::StorageLedger::Ticket ticket;
	int lease = -1, image = -1;
	void *address = MAP_FAILED;
	std::size_t length = 0;
	bool retry = false;
	explicit Mapping(std::shared_ptr<distribution::StorageLedger> ledger) : account(std::move(ledger)) {}
	bool retire_step() noexcept override {
		retry = false;
		if (address != MAP_FAILED) {
			if (::munmap(address, length) != 0) {
				retry = true;
				return false; // Preserve the mapping and charge for reconciliation.
			}
			address = MAP_FAILED;
			ticket.reset();
			return false;
		}
		if (image >= 0) {
			::close(std::exchange(image, -1));
			return false;
		}
		if (lease >= 0) {
			::close(std::exchange(lease, -1));
			return false;
		}
		ticket.reset(); // Failed mmap reserved no actual mapped region.
		return true;
	}
	bool retry_later() const noexcept override { return retry; }
};
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
RecoveredGeneration recover_generation(const std::filesystem::path &directory, ViewLimits limits,
									   std::optional<distribution::Cursor> expected) {
	return recover_generation(directory, limits, expected, nullptr);
}
RecoveredGeneration recover_generation(const std::filesystem::path &directory, ViewLimits limits,
									   std::optional<distribution::Cursor> expected,
									   const RecoveredGeneration *prior) {
	auto &domain = resources::domain(limits);
	const auto mapping_account = resources::mapping_account(limits);
	auto directory_owner = domain.make<Directory>();
	using DirectoryLease = distribution::RetirementHandle<Directory>;
	auto directory_lease = std::allocate_shared<DirectoryLease>(
		distribution::StorageAllocator<DirectoryLease>(domain.storage_account()), directory_owner);
	std::shared_ptr<File> dir(directory_lease, &directory_owner->file);
	dir->fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	struct stat directory_info{};
	require(dir->fd >= 0 && ::fstat(dir->fd, &directory_info) == 0 && S_ISDIR(directory_info.st_mode) &&
			!(directory_info.st_mode & 0022));
	for (unsigned attempt = 0; attempt < 3; ++attempt) {
		File selected(::openat(dir->fd, "selected", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
		trusted_file(selected.fd, directory_info.st_uid, descriptor_bytes);
		std::array<char, descriptor_bytes> raw{};
		read_record(selected.fd, raw);
		auto descriptor = decode_descriptor(raw);
		require(descriptor.length <= limits.bytes);
		auto name = generation_name(descriptor.generation);
		auto mapping = domain.make<Mapping>(mapping_account);
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
		mapping->image = ::openat(dir->fd, (name + ".image").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
		trusted_file(mapping->image, directory_info.st_uid, descriptor.length);
		mapping->length = descriptor.length;
		mapping->ticket = mapping->account->admit(mapping_capacity(descriptor.length));
		mapping->address = ::mmap(nullptr, mapping->length, PROT_READ, MAP_SHARED, mapping->image, 0);
		require(mapping->address != MAP_FAILED);
		auto bytes = std::span(static_cast<const char *>(mapping->address), mapping->length);
		SHA256 hash;
		for (std::size_t at = 0; at < bytes.size();) {
			auto count = std::min<std::size_t>(65536, bytes.size() - at);
			hash.add(bytes.data() + at, count);
			at += count;
		}
		unsigned char digest[32];
		hash.getHash(digest);
		require(std::memcmp(digest, descriptor.checksum.data(), 32) == 0);
		if (generation_ownership && generation_ownership->layout_version == 2) {
			paged::Limits bounds;
			bounds.bytes = std::min(bounds.bytes, limits.bytes - descriptor.length);
			bounds.records = limits.records;
			bounds.namespaces = limits.namespaces;
			const auto root = paged::decode_root(bytes, bounds);
			require(root.cursor == descriptor.cursor && root.profile == descriptor.profile &&
					(!expected ||
					 (expected->same_identity(root.cursor) && root.cursor.revision >= expected->revision)));
			const auto storage = generation_ownership->storage;
			auto account = domain.storage_account();
			const DirectoryIdentity identity{std::uint64_t(directory_info.st_dev),
											 std::uint64_t(directory_info.st_ino)};
			bool warm = prior && prior->paged_ && prior->directory_identity() == identity &&
						prior->paged_->storage_identity() == storage &&
						detail::reuses_paged_state(prior->paged_->root(), root) &&
						prior->paged_->registry()->uses_account(account);
			if (warm) {
				const auto registry = prior->paged_->registry();
				for (std::size_t i = 0; i < registry->inputs().size(); ++i)
					if (registry->mapping(i)->storage_account() != mapping_account) {
						warm = false;
						break;
					}
			}
			std::optional<detail::DependencyDiscovery> discovery;
			distribution::RetirementHandle<detail::SegmentRegistry> registry;
			if (warm) {
				registry = prior->paged_->registry();
				const auto counts = prior->paged_->counts();
				require(counts.strings <= limits.strings && counts.maximum_overrides <= limits.overrides &&
						registry->physical_bytes() <= bounds.bytes);
			} else {
				discovery.emplace(account, domain, root, storage, bounds);
				while (!discovery->done()) {
					ValidationBudget budget;
					discovery->step(budget);
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
						auto segment = domain.make<detail::SegmentMapping>(mapping_account, payload.fd,
																		   directory_info.st_uid, ownership);
						discovery->supply(std::move(segment));
					}
				}
				registry = discovery->take();
			}

			using RootLease = distribution::RetirementHandle<Mapping>;
			auto root_lease =
				std::allocate_shared<RootLease>(distribution::StorageAllocator<RootLease>(account), mapping);

			auto capability = domain.make<detail::LeasedPagedRoot>(dir, root_lease, registry, descriptor,
																   identity, storage, root);
			using Capability = distribution::RetirementHandle<detail::LeasedPagedRoot>;
			std::optional<detail::GraphValidator> validator;
			if (!warm) {
				auto owner = std::allocate_shared<Capability>(
					distribution::StorageAllocator<Capability>(account), capability);
				validator.emplace(account, owner, registry->inputs(), root, storage, bounds,
								  detail::PagedSemanticLimits{limits.strings, limits.overrides});
				while (!validator->done()) {
					ValidationBudget budget;
					validator->step(budget);
				}
				for (;;) {
					ValidationBudget budget;
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
			require(::fstat(mapping->image, &held_image) == 0 &&
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
			if (warm)
				capability->seal_progress(*prior->paged_);
			else
				capability->seal(validator->counts());
			if (discovery)
				for (;;) {
					ValidationBudget budget;
					if (discovery->discard(budget))
						break;
				}
			auto view = warm ? detail::PagedProgressView::take(domain, capability) : validator->take(domain);
			return RecoveredGeneration(std::move(view), descriptor, dir, identity, std::move(capability));
		}
		auto header = decode_header(bytes, limits.bytes);
		require(header.cursor == descriptor.cursor && header.profile == descriptor.profile);
		using Lease = distribution::RetirementHandle<Mapping>;
		auto lease = std::allocate_shared<Lease>(
			distribution::StorageAllocator<Lease>(domain.storage_account()), mapping);
		ImageValidator validator(lease, bytes, limits, expected);
		while (!validator.done()) {
			ValidationBudget budget;
			validator.step(budget);
		}
		return RecoveredGeneration(
			validator.take(), descriptor, dir,
			{std::uint64_t(directory_info.st_dev), std::uint64_t(directory_info.st_ino)});
	}
	throw std::runtime_error("local generation changed while acquiring its lease");
}

ReadView open_generation(const std::filesystem::path &directory, ViewLimits limits,
						 std::optional<distribution::Cursor> expected) {
	return recover_generation(directory, limits, expected).view();
}
} // namespace detent::local
