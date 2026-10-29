#include "generation_file_access.hh"
#include "names.hh"
#include "paged_root_prefix.hh"
#include "prepared_reader.hh"
#include "reader_capsule.hh"
#include <iostream>
#include <sys/file.h>

namespace l = detent::local;
namespace x = l::detail;
namespace p = l::paged;
void require(bool value) {
	if (!value) throw std::runtime_error("copied-fixture capsule conversion refused");
}
template <std::size_t N>
std::array<char, N> record(int directory, std::string_view name) {
	x::RecoveryFile file(::openat(directory, std::string(name).c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
	x::check_generation_file(file.fd, ::getuid(), N);
	require(detent::internal::mode_only_acl(file.fd));
	std::array<char, N> bytes{};
	x::read_generation_record(file.fd, bytes);
	return bytes;
}
int main(int argc, char **argv) {
	try {
		require(argc == 3 && std::string_view(argv[1]) == "--test-only-copied-fixture");
		const std::filesystem::path path(argv[2]);
		x::RecoveryFile directory(x::open_publisher_directory(path, ::getuid()));
		const auto descriptor = l::decode_descriptor(record<l::descriptor_bytes>(directory.fd, "selected"));
		const auto storage = l::decode_storage_identity(record<l::storage_identity_bytes>(directory.fd, "storage.meta"));
		const x::GenerationName lease_name(descriptor.generation, x::GenerationFile::Lease);
		x::RecoveryFile lease(::openat(directory.fd, std::string(lease_name.view()).c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW));
		x::check_generation_file(lease.fd, ::getuid(), l::generation_ownership_bytes);
		require(detent::internal::mode_only_acl(lease.fd));
		require(::flock(lease.fd, LOCK_EX | LOCK_NB) == 0);
		struct stat held{}, named{};
		require(::fstat(lease.fd, &held) == 0 &&
		        ::fstatat(directory.fd, std::string(lease_name.view()).c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
		        held.st_dev == named.st_dev && held.st_ino == named.st_ino);
		std::array<char, l::generation_ownership_bytes> lease_bytes{};
		x::read_generation_record(lease.fd, lease_bytes);
		const auto ownership = l::decode_generation_ownership(lease_bytes);
		require(ownership.storage == storage && ownership.generation == descriptor.generation &&
		        ownership.layout_version == 2 && ownership.image_length == descriptor.length &&
		        descriptor.length == p::root_bytes && ownership.image_checksum == descriptor.checksum);
		const x::GenerationName image_name(descriptor.generation, x::GenerationFile::Image);
		const auto image = record<p::root_bytes>(directory.fd, image_name.view());
		const auto root = p::decode_root(image, x::paged_format_limits());
		require(p::physical_commitment(image) == descriptor.checksum && root.cursor == descriptor.cursor &&
		        root.profile == descriptor.profile);
		const x::GenerationName metadata_name(descriptor.generation, x::GenerationFile::ReaderMetadata);
		const auto metadata = x::decode_reader_metadata(record<x::reader_metadata_bytes>(directory.fd, metadata_name.view()));
		require(metadata.generation == descriptor.generation);
		const auto bytes = x::encode_reader_capsule({storage, root, metadata});
		(void)x::decode_reader_capsule(bytes);
		const auto staging = std::string(metadata_name.view()) + ".test-staging";
		x::RecoveryFile output(::openat(directory.fd, staging.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
		require(output.fd >= 0);
		for (std::size_t offset = 0; offset < bytes.size();) {
			const auto n = ::write(output.fd, bytes.data() + offset, bytes.size() - offset);
			if (n < 0 && errno == EINTR) continue;
			require(n > 0);
			offset += n;
		}
		require(::fsync(output.fd) == 0);
		require(l::decode_descriptor(record<l::descriptor_bytes>(directory.fd, "selected")) == descriptor);
		require(::renameat(directory.fd, staging.c_str(), directory.fd, std::string(metadata_name.view()).c_str()) == 0);
		require(::fsync(directory.fd) == 0);
		std::cout << "converted copied fixture to 624-byte capsule\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
