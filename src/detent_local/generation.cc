#include "detent_local/generation.hh"
#include "resources.hh"
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
	constexpr char hex[] = "0123456789abcdef";
	std::string name = "generation-";
	name.reserve(43);
	for (unsigned char c : id) {
		name += hex[c >> 4];
		name += hex[c & 15];
	}
	return name;
}
RecoveredGeneration recover_generation(const std::filesystem::path &directory, ViewLimits limits,
									   std::optional<distribution::Cursor> expected) {
	auto &domain = resources::domain(limits);
	auto dir = std::allocate_shared<File>(distribution::StorageAllocator<File>(domain.storage_account()), -1);
	dir->fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	struct stat directory_info{};
	require(dir->fd >= 0 && ::fstat(dir->fd, &directory_info) == 0 && S_ISDIR(directory_info.st_mode) &&
			!(directory_info.st_mode & 0022));
	for (unsigned attempt = 0; attempt < 3; ++attempt) {
		File selected(::openat(dir->fd, "selected", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
		trusted_file(selected.fd, directory_info.st_uid, descriptor_bytes);
		std::array<char, descriptor_bytes> raw{};
		std::size_t offset = 0;
		while (offset < raw.size()) {
			auto n = ::pread(selected.fd, raw.data() + offset, raw.size() - offset, offset);
			if (n < 0 && errno == EINTR)
				continue;
			require(n > 0);
			offset += n;
		}
		auto descriptor = decode_descriptor(raw);
		require(descriptor.length <= limits.bytes);
		auto name = generation_name(descriptor.generation);
		auto mapping = domain.make<Mapping>(resources::mapping_account(limits));
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
		trusted_file(mapping->lease, directory_info.st_uid, 0);
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
