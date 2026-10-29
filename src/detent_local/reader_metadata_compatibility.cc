#include "detent_local/storage_session.hh"
#include "generation_file_access.hh"
#include "names.hh"
#include "reader_metadata.hh"
#include <dirent.h>

namespace detent::local {
namespace {
void require(bool value) {
	if (!value)
		throw std::runtime_error("legacy metadata verification refused");
}
template <std::size_t N> std::array<char, N> record(int directory, std::string_view name) {
	const std::string terminated(name);
	detail::RecoveryFile file(
		::openat(directory, terminated.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
	detail::check_generation_file(file.fd, ::geteuid(), N);
	require(internal::mode_only_acl(file.fd));
	struct stat held{}, named{}, after{};
	require(::fstat(file.fd, &held) == 0);
	std::array<char, N> bytes{};
	detail::read_generation_record(file.fd, bytes);
	require(::fstat(file.fd, &after) == 0 &&
			::fstatat(directory, terminated.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
			held.st_dev == after.st_dev && held.st_ino == after.st_ino &&
			held.st_size == after.st_size && held.st_uid == after.st_uid &&
			held.st_mode == after.st_mode && after.st_nlink == 1 && after.st_dev == named.st_dev &&
			after.st_ino == named.st_ino && internal::mode_only_acl(file.fd));
	return bytes;
}
struct Directory {
	DIR *value = nullptr;
	~Directory() {
		if (value)
			::closedir(value);
	}
};
} // namespace
void verify_legacy_reader_metadata(const std::filesystem::path &path,
								   const StorageSession &session) {
	require(!session.closed());
	detail::RecoveryFile directory(detail::open_publisher_directory(path, ::geteuid()));
	struct stat info{};
	require(::fstat(directory.fd, &info) == 0 &&
			session.directory_identity() ==
				DirectoryIdentity{std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)});
	const auto selected = record<descriptor_bytes>(directory.fd, "selected");
	const auto descriptor = decode_descriptor(selected);
	const auto storage =
		decode_storage_identity(record<storage_identity_bytes>(directory.fd, "storage.meta"));
	const auto scan_fd =
		::openat(directory.fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	require(scan_fd >= 0);
	Directory scan{::fdopendir(scan_fd)};
	if (!scan.value) {
		::close(scan_fd);
		require(false);
	}
	bool selected_metadata = false;
	for (;;) {
		errno = 0;
		const auto entry = ::readdir(scan.value);
		if (!entry) {
			require(errno == 0);
			break;
		}
		const std::string_view name(entry->d_name);
		if (!name.ends_with(".read"))
			continue;
		const auto parsed = detail::parse_generation_name(name);
		require(parsed && parsed->kind == detail::GenerationFile::ReaderMetadata);
		try {
			const auto metadata =
				detail::decode_reader_metadata(record<prepared_metadata_bytes>(directory.fd, name));
			require(metadata.storage == storage && metadata.generation == parsed->generation);
			if (parsed->generation == descriptor.generation) {
				require(metadata.root_checksum == descriptor.checksum);
				selected_metadata = true;
			}
		} catch (const std::exception &error) {
			throw std::runtime_error("legacy metadata verification blocked by " +
									 std::string(name) + ": " + error.what());
		}
	}
	require(selected_metadata && record<descriptor_bytes>(directory.fd, "selected") == selected);
}
} // namespace detent::local
