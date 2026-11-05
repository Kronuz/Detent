#include "detent_local/read_counters.hh"
#include "prepared_reader.hh"
#include "generation_file_access.hh"
#include "lazy_page_access.hh"
#include "leased_paged_root.hh"
#include "names.hh"
#include "paged_root_prefix.hh"
#include "read_root.hh"
#include "resources.hh"
#include <limits>
#include <sha256.h>
#include <sys/file.h>

namespace detent::local::detail {
namespace d = distribution;
namespace {
thread_local void (*record_hook)(std::string_view, void *) = nullptr;
thread_local void *record_context = nullptr;
struct PreparedFiles final : d::RetirementNode {
	RecoveryFile directory{-1}, lease{-1};
};
void require(bool value) {
	if (!value)
		throw ReadSourceFailure("invalid daemon-prepared generation");
}
int open_record(int directory, std::string_view name) {
	DETENT_COUNT(record_opens, 1);
	require(name.size() <= 64 && name.find('\0') == std::string_view::npos);
	std::array<char, 65> terminated{};
	std::copy(name.begin(), name.end(), terminated.begin());
	return ::openat(directory, terminated.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
}
// A file a publish or collection unlinked after it was opened: a race to retry, not a bad store.
bool unlinked(int fd) {
	struct stat info{};
	return fd >= 0 && ::fstat(fd, &info) == 0 && info.st_nlink == 0;
}
// check_generation_file, except that a file unlinked under it reports false to retry.
bool checked_or_unlinked(int fd, uid_t owner, std::uint64_t length) {
	try {
		check_generation_file(fd, owner, length);
		return true;
	} catch (const std::runtime_error &) {
		if (unlinked(fd))
			return false;
		throw;
	}
}
template <std::size_t N> std::array<char, N> record(int directory, std::string_view name, uid_t owner) {
	// A publish replaces `selected` by rename: a read that opened the replaced file opens it again.
	for (unsigned attempt = 0; attempt != 3; ++attempt) {
		RecoveryFile file(open_record(directory, name));
		if (record_hook)
			record_hook(name, record_context);
		if (file.fd < 0 && errno == ENOENT && (name == "selected" || name.ends_with(".read")))
			throw GenerationUnavailable("publisher has not prepared a selected generation");
		if (!checked_or_unlinked(file.fd, owner, N))
			continue;
		require(internal::mode_only_acl(file.fd));
		std::array<char, N> bytes{};
		read_generation_record(file.fd, bytes);
		return bytes;
	}
	throw GenerationUnavailable("publisher replaced the record during the read");
}
paged::Digest checksum(std::span<const char> bytes) {
	DETENT_COUNT(bytes_hashed, bytes.size());
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<unsigned char, 32> digest{};
	hash.getHash(digest.data());
	paged::Digest result;
	std::copy(digest.begin(), digest.end(), result.begin());
	return result;
}
} // namespace
void PreparedReaderTesting::install(void (*hook)(std::string_view, void *), void *context) noexcept {
	record_hook = hook;
	record_context = context;
}
GenerationSelection PreparedReader::selection(const std::filesystem::path &path, uid_t publisher) {
    RecoveryFile directory(open_publisher_directory(path, publisher));
    struct stat info{};
    require(::fstat(directory.fd, &info) == 0);
    return {selected(directory.fd, publisher), {std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)}};
}
ReaderMetadata PreparedReader::describe(const RecoveredGeneration &generation) {
	if (!generation.paged_)
		throw std::invalid_argument("prepared metadata requires a verified paged generation");
	const auto &base = *generation.paged_;
	const auto physical = base.registry()->physical_bytes();
	if (physical > UINT64_MAX - paged::root_bytes)
		throw std::overflow_error("prepared generation physical bytes overflow");
	return {base.storage_identity(), generation.descriptor().generation, generation.descriptor().checksum,
	        base.counts(), physical + paged::root_bytes};
}
bool PreparedReader::metadata_ready(const std::filesystem::path &path,
                                    const RecoveredGeneration &generation, uid_t publisher) {
    const auto expected = encode_reader_metadata(describe(generation));
    RecoveryFile directory(open_publisher_directory(path, publisher));
    struct stat held{};
    require(directory.fd >= 0 && ::fstat(directory.fd, &held) == 0 && S_ISDIR(held.st_mode) &&
            held.st_uid == publisher && !(held.st_mode & 0022) &&
            internal::acceptable_acl(directory.fd, internal::AclPolicy::DenialsAllowed));
    const auto identity = generation.directory_identity();
    require(std::uint64_t(held.st_dev) == identity.device && std::uint64_t(held.st_ino) == identity.inode);
    const GenerationName name(generation.descriptor().generation, GenerationFile::ReaderMetadata);
    RecoveryFile file(open_record(directory.fd, name.view()));
    if (file.fd < 0 && errno == ENOENT)
        return false;
    check_generation_file(file.fd, publisher, reader_metadata_bytes);
    require(internal::mode_only_acl(file.fd));
    std::array<char, reader_metadata_bytes> bytes{};
    read_generation_record(file.fd, bytes);
    decode_reader_metadata(bytes);
    require(bytes == expected);
    return true;
}
AttachedGeneration::AttachedGeneration(AttachedGeneration &&other) noexcept
    : lease(std::exchange(other.lease, -1)), descriptor(other.descriptor), storage(other.storage),
      root(other.root), metadata(other.metadata), limits(std::move(other.limits)) {}
AttachedGeneration::~AttachedGeneration() {
	if (lease >= 0)
		::close(lease);
}
Descriptor PreparedReader::selected(int directory, uid_t publisher) {
	return decode_descriptor(record<descriptor_bytes>(directory, "selected", publisher));
}
AttachedGeneration PreparedReader::attach(int directory, uid_t publisher, ViewLimits limits,
                                          std::optional<d::Cursor> expected) {
	for (unsigned attempt = 0; attempt != 3; ++attempt) {
		const auto descriptor = selected(directory, publisher);
		if (expected)
			require(expected->same_identity(descriptor.cursor) &&
			        descriptor.cursor.revision >= expected->revision);
		const GenerationName lease_name(descriptor.generation, GenerationFile::Lease);
		RecoveryFile lease(open_record(directory, lease_name.view()));
		if (lease.fd < 0 && errno == ENOENT)
			continue;
		if (!checked_or_unlinked(lease.fd, publisher, generation_ownership_bytes))
			continue; // collected between the open and the check
		require(internal::mode_only_acl(lease.fd));
		int locked;
		do {
			locked = ::flock(lease.fd, LOCK_SH | LOCK_NB);
		} while (locked < 0 && errno == EINTR);
		if (locked < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
			continue;
		require(locked == 0);
		std::array<char, 65> terminated_lease{};
		std::copy(lease_name.view().begin(), lease_name.view().end(), terminated_lease.begin());
		struct stat held{}, named{};
		require(::fstat(lease.fd, &held) == 0);
		if (!held.st_nlink)
			continue;
		if (::fstatat(directory, terminated_lease.data(), &named, AT_SYMLINK_NOFOLLOW) != 0) {
			if (errno == ENOENT)
				continue;
			require(false);
		}
		if (held.st_dev != named.st_dev || held.st_ino != named.st_ino)
			continue;
		std::array<char, generation_ownership_bytes> lease_bytes{};
		read_generation_record(lease.fd, lease_bytes);
		const auto ownership = decode_generation_ownership(lease_bytes);
		const auto storage =
		    decode_storage_identity(record<storage_identity_bytes>(directory, "storage.meta", publisher));
		require(ownership.storage == storage && ownership.generation == descriptor.generation &&
		        ownership.image_length == descriptor.length && ownership.image_checksum == descriptor.checksum);
		if (ownership.layout_version == 1)
			throw GenerationUnavailable("publisher has not prepared a paged generation");
		require(ownership.layout_version == 2 && descriptor.length == paged::root_bytes);
		const GenerationName image_name(descriptor.generation, GenerationFile::Image);
		const auto image = record<paged::root_bytes>(directory, image_name.view(), publisher);
		require(checksum(image) == descriptor.checksum);
		const auto root = paged::decode_root(image, paged_format_limits());
		require(root.cursor == descriptor.cursor && root.profile == descriptor.profile);
		const GenerationName metadata_name(descriptor.generation, GenerationFile::ReaderMetadata);
		const auto metadata = decode_reader_metadata(
		    record<reader_metadata_bytes>(directory, metadata_name.view(), publisher));
		require(metadata.storage == storage && metadata.generation == descriptor.generation &&
		        metadata.root_checksum == descriptor.checksum &&
		        std::uint64_t(metadata.counts.bools) + metadata.counts.ints == root.records &&
		        metadata.physical_bytes >= paged::root_bytes &&
		        root.bytes <= metadata.physical_bytes - paged::root_bytes);
		require(metadata.physical_bytes <= limits.bytes && metadata.counts.strings <= limits.strings &&
		        root.records <= limits.records && root.namespaces <= limits.namespaces &&
		        metadata.counts.maximum_overrides <= limits.overrides && root.pages <= limits.pages &&
		        root.segments <= limits.segments);
		limits.bytes = metadata.physical_bytes;
		limits.records = root.records;
		limits.namespaces = root.namespaces;
		limits.strings = metadata.counts.strings;
		limits.overrides = metadata.counts.maximum_overrides;
		limits.pages = root.pages;
		limits.segments = root.segments;
		AttachedGeneration attached;
		attached.lease = std::exchange(lease.fd, -1);
		attached.descriptor = descriptor;
		attached.storage = storage;
		attached.root = root;
		attached.metadata = metadata;
		attached.limits = std::move(limits);
		return attached;
	}
	throw GenerationUnavailable("selected generation changed during attachment");
}
PreparedGeneration PreparedReader::open(const std::filesystem::path &path, uid_t publisher, ViewLimits limits,
                                        std::optional<d::Cursor> expected) {
	auto &domain = resources::domain(limits);
	auto factory = domain.factory();
	auto heap = domain.storage_account();
	auto files = factory.make<PreparedFiles>();
	files->directory.fd = open_publisher_directory(path, publisher);
	struct stat directory{};
	require(::fstat(files->directory.fd, &directory) == 0);
	const DirectoryIdentity directory_identity{std::uint64_t(directory.st_dev), std::uint64_t(directory.st_ino)};
	auto attached = attach(files->directory.fd, publisher, limits, expected);
	files->lease.fd = std::exchange(attached.lease, -1);
	auto mappings = resources::mapping_account(attached.limits);
	using Files = d::RetirementHandle<PreparedFiles>;
	auto files_owner = std::allocate_shared<Files>(d::StorageAllocator<Files>(heap), files);
	auto access = factory.make<LazyPageAccess>(files_owner, files->directory.fd, publisher, attached.storage,
	                                           attached.root, attached.limits, heap, mappings);
	using Access = d::RetirementHandle<LazyPageAccess>;
	auto access_owner = std::allocate_shared<Access>(d::StorageAllocator<Access>(heap), std::move(access));
	std::shared_ptr<const PagedPageAccess> page_access(access_owner, access_owner->get());
	const auto &counts = attached.metadata.counts;
	auto view = factory.make<ReadView::Root>(access_owner,
	                                         PagedReadState{attached.root, {}, std::move(page_access)},
	                                         counts.bools, counts.ints, counts.overrides, counts.strings);
	return {ReadView(std::move(view)), attached.descriptor, directory_identity, attached.root.logical};
}
} // namespace detent::local::detail
