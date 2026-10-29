#include "prepared_reader.hh"
#include "reader_capsule.hh"
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
struct PreparedFiles final : d::RetirementNode {
	RecoveryFile directory{-1}, lease{-1};
};
void require(bool value) {
	if (!value)
		throw ReadSourceFailure("invalid daemon-prepared generation");
}
int open_record(int directory, std::string_view name) {
	require(name.size() <= 64 && name.find('\0') == std::string_view::npos);
	std::array<char, 65> terminated{};
	std::copy(name.begin(), name.end(), terminated.begin());
	return ::openat(directory, terminated.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
}
template <std::size_t N> std::array<char, N> record(int directory, std::string_view name, uid_t owner) {
	RecoveryFile file(open_record(directory, name));
    if (file.fd < 0 && errno == ENOENT &&
        (name == "selected" || name.ends_with(".read")))
        throw GenerationUnavailable("publisher has not prepared a selected generation");
	check_generation_file(file.fd, owner, N);
	require(internal::mode_only_acl(file.fd));
	std::array<char, N> bytes{};
	read_generation_record(file.fd, bytes);
	return bytes;
}
paged::Digest checksum(std::span<const char> bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<unsigned char, 32> digest{};
	hash.getHash(digest.data());
	paged::Digest result;
	std::copy(digest.begin(), digest.end(), result.begin());
	return result;
}
} // namespace
GenerationSelection PreparedReader::selection(const std::filesystem::path &path, uid_t publisher) {
    RecoveryFile directory(open_publisher_directory(path, publisher));
    struct stat info{};
    require(::fstat(directory.fd, &info) == 0);
    const auto bytes = record<descriptor_bytes>(directory.fd, "selected", publisher);
    return {decode_descriptor(bytes), {std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)}};
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
                                    const RecoveredGeneration &generation, uid_t publisher,
                                    std::optional<ReaderMetadataEncoding> required) {
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
    struct stat info{};
    require(file.fd >= 0 && ::fstat(file.fd, &info) == 0);
    const bool capsule = info.st_size == reader_capsule_bytes;
    check_generation_file(file.fd, publisher, capsule ? reader_capsule_bytes : reader_metadata_bytes);
    require(internal::mode_only_acl(file.fd));
    if (capsule) {
        std::array<char, reader_capsule_bytes> bytes{};
        read_generation_record(file.fd, bytes);
        const auto decoded = decode_reader_capsule(bytes);
        require(encode_reader_metadata(decoded.metadata) == expected &&
                decoded.storage == generation.paged_->storage_identity() &&
                paged::encode_root(decoded.root, paged_format_limits()) ==
                    paged::encode_root(generation.paged_->root(), paged_format_limits()));
    } else {
        std::array<char, reader_metadata_bytes> bytes{};
        read_generation_record(file.fd, bytes);
        decode_reader_metadata(bytes);
        require(bytes == expected);
    }
    const auto actual = capsule ? ReaderMetadataEncoding::Capsule : ReaderMetadataEncoding::Legacy;
    return !required || *required == actual;
}
PreparedGeneration PreparedReader::open(const std::filesystem::path &path, uid_t publisher, ViewLimits limits,
                                        std::optional<d::Cursor> expected) {
	auto &domain = resources::domain(limits);
	auto factory = domain.factory();
	auto heap = domain.storage_account();
	for (unsigned attempt = 0; attempt != 3; ++attempt) {
		auto files = factory.make<PreparedFiles>();
		files->directory.fd = open_publisher_directory(path, publisher);
		struct stat directory{};
		require(::fstat(files->directory.fd, &directory) == 0);
		const DirectoryIdentity directory_identity{std::uint64_t(directory.st_dev),
		                                           std::uint64_t(directory.st_ino)};
		const auto selected = record<descriptor_bytes>(files->directory.fd, "selected", publisher);
		const auto descriptor = decode_descriptor(selected);
		if (expected)
			require(expected->same_identity(descriptor.cursor) &&
			        descriptor.cursor.revision >= expected->revision);
		const GenerationName lease_name(descriptor.generation, GenerationFile::Lease);
		files->lease.fd = open_record(files->directory.fd, lease_name.view());
		if (files->lease.fd < 0 && errno == ENOENT)
			continue;
		check_generation_file(files->lease.fd, publisher, generation_ownership_bytes);
		require(internal::mode_only_acl(files->lease.fd));
		int locked;
		do {
			locked = ::flock(files->lease.fd, LOCK_SH | LOCK_NB);
		} while (locked < 0 && errno == EINTR);
		if (locked < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
			continue;
		require(locked == 0);
		std::array<char, 65> terminated_lease{};
		std::copy(lease_name.view().begin(), lease_name.view().end(), terminated_lease.begin());
		struct stat held{}, named{};
		require(::fstat(files->lease.fd, &held) == 0);
		if (!held.st_nlink)
			continue;
		if (::fstatat(files->directory.fd, terminated_lease.data(), &named, AT_SYMLINK_NOFOLLOW) != 0) {
			if (errno == ENOENT)
				continue;
			require(false);
		}
		if (held.st_dev != named.st_dev || held.st_ino != named.st_ino)
			continue;
		std::array<char, generation_ownership_bytes> lease{};
		read_generation_record(files->lease.fd, lease);
		const auto ownership = decode_generation_ownership(lease);
		const GenerationName metadata_name(descriptor.generation, GenerationFile::ReaderMetadata);
		RecoveryFile metadata_file(open_record(files->directory.fd, metadata_name.view()));
		const auto metadata_error = errno;
		struct stat metadata_info{};
		const bool metadata_inspected = metadata_file.fd >= 0 && ::fstat(metadata_file.fd, &metadata_info) == 0;
		const bool capsule = metadata_inspected && metadata_info.st_size == reader_capsule_bytes;
		StorageIdentity storage{};
		paged::Root root;
		ReaderMetadata metadata;
		if (capsule) {
			check_generation_file(metadata_file.fd, publisher, reader_capsule_bytes);
			require(internal::mode_only_acl(metadata_file.fd));
			std::array<char, reader_capsule_bytes> bytes{};
			read_generation_record(metadata_file.fd, bytes);
			auto decoded = decode_reader_capsule(bytes);
			storage = decoded.storage;
			root = decoded.root;
			metadata = decoded.metadata;
		} else {
			// Legacy refusals validate originals before reporting absent or invalid metadata.
			storage = decode_storage_identity(
			    record<storage_identity_bytes>(files->directory.fd, "storage.meta", publisher));
		}
		require(ownership.storage == storage && ownership.generation == descriptor.generation &&
		        ownership.image_length == descriptor.length && ownership.image_checksum == descriptor.checksum);
		if (ownership.layout_version == 1)
			throw GenerationUnavailable("publisher has not prepared a paged generation");
		require(ownership.layout_version == 2 && descriptor.length == paged::root_bytes);
		if (!capsule) {
			const GenerationName image_name(descriptor.generation, GenerationFile::Image);
			const auto image = record<paged::root_bytes>(files->directory.fd, image_name.view(), publisher);
			require(checksum(image) == descriptor.checksum);
			root = paged::decode_root(image, paged_format_limits());
			require(root.cursor == descriptor.cursor && root.profile == descriptor.profile);
			if (metadata_file.fd < 0 && metadata_error == ENOENT)
				throw GenerationUnavailable("publisher has not prepared a selected generation");
			require(metadata_inspected);
			check_generation_file(metadata_file.fd, publisher, reader_metadata_bytes);
			require(internal::mode_only_acl(metadata_file.fd));
			std::array<char, reader_metadata_bytes> bytes{};
			read_generation_record(metadata_file.fd, bytes);
			metadata = decode_reader_metadata(bytes);
		}
		require(root.cursor == descriptor.cursor && root.profile == descriptor.profile);
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
		auto mappings = resources::mapping_account(limits);
		using Files = d::RetirementHandle<PreparedFiles>;
		auto files_owner = std::allocate_shared<Files>(d::StorageAllocator<Files>(heap), files);
		auto access = factory.make<LazyPageAccess>(files_owner, files->directory.fd, publisher, storage, root,
		                                           limits, factory, heap, mappings);
		using Access = d::RetirementHandle<LazyPageAccess>;
		auto access_owner =
		    std::allocate_shared<Access>(d::StorageAllocator<Access>(heap), std::move(access));
		std::shared_ptr<const PagedPageAccess> page_access(access_owner, access_owner->get());
		auto view = factory.make<ReadView::Root>(
		    access_owner, PagedReadState{root, {}, std::move(page_access)}, metadata.counts.bools,
		    metadata.counts.ints, metadata.counts.overrides, metadata.counts.strings);
		return {ReadView(std::move(view)), descriptor, directory_identity, root.logical};
	}
	throw GenerationUnavailable("selected generation changed during attachment");
}
} // namespace detent::local::detail
