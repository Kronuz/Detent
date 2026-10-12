#include "paged_disk_fixture.hh"
#include "paged_named_root.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <fcntl.h>
#include <sys/file.h>
#include <thread>
namespace c = kronuz::io::completion;
void drive(l::detail::PagedNamedRoot &reader) {
	for (unsigned turns = 0; !reader.done(); ++turns) {
		require(turns < 1000);
		reader.submitted();
		require(reader.complete(kronuz::journal::detail::execute_primitive(reader.io(), reader.request())));
	}
}
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-named-root-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	install(directory, image, storage);
	auto account = std::make_shared<d::StorageLedger>(32 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 64);
	const auto initial = account->used();
	l::ViewLimits limits;
	limits.retirement_domain = &domain;
	limits.mapping_account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	{
		auto certificate = domain.make<l::detail::PagedDependencyCertificate>(account, domain.factory());
		{
			auto actual = l::recover_generation(directory, limits);
			certificate->capture(actual);
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (limits.mapping_account->used()) {
			require(std::chrono::steady_clock::now() < deadline);
			std::this_thread::yield();
		}
		require(std::filesystem::remove(directory / "selected"));
		auto other_root = image.root();
		++other_root.cursor.revision;
		const auto other_bytes = l::paged::encode_root(other_root);
		l::GenerationId other_generation;
		other_generation.fill('n');
		const l::Descriptor other_descriptor{other_generation, l::paged::root_bytes,
											 l::paged::physical_commitment(other_bytes), other_root.cursor,
											 other_root.profile};
		const auto other_name = l::generation_name(other_generation);
		write(directory / (other_name + ".image"), other_bytes);
		write(directory / (other_name + ".lease"),
			  l::encode_generation_ownership(
				  {storage, other_generation, other_descriptor.length, other_descriptor.checksum, 2}));
		write(directory / "selected", l::encode_descriptor(other_descriptor));
		auto other = domain.make<l::detail::PagedDependencyCertificate>(account, domain.factory());
		{
			auto actual = l::recover_generation(directory, limits);
			other->capture(actual);
		}
		require(std::filesystem::remove(directory / "selected"));
		const auto other_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (limits.mapping_account->used()) {
			require(std::chrono::steady_clock::now() < other_deadline);
			std::this_thread::yield();
		}
		auto session = l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), true);
		const auto name = l::generation_name(generation);
		const auto image_path = directory / (name + ".image");
		const auto lease_path = directory / (name + ".lease");
		const auto original = std::span<const char>(image.bytes()->data(), image.bytes()->size());
		const l::GenerationOwnership ownership{storage, generation, image.descriptor().length,
											   image.descriptor().checksum, 2};
		for (unsigned variant = 0; variant < 8; ++variant) {
			write(image_path, original);
			write(lease_path, l::encode_generation_ownership(ownership));
			if (variant == 2) {
				std::array<char, l::paged::root_bytes> corrupt;
				std::copy(original.begin(), original.end(), corrupt.begin());
				corrupt[56] ^= 1;
				write(image_path, corrupt);
			}
			l::detail::PagedNamedRoot reader(
				domain.factory(), session, storage, generation, ::getuid() + (variant == 3),
				variant == 1   ? d::RetirementHandle<l::detail::PagedDependencyCertificate>{}
				: variant == 7 ? other
							   : certificate);
			bool fired = false;
			const auto injected =
				std::make_exception_ptr(std::runtime_error("named root read cancelled with error"));
			for (unsigned turns = 0; !reader.done(); ++turns) {
				require(turns < 1000);
				const auto &request = reader.request();
				const bool root_read = request.kind == c::Kind::Read && request.source == name + ".image";
				reader.submitted();
				c::Result result;
				if (variant == 6 && root_read && !fired) {
					reader.close();
					result.token = request.token;
					result.error = injected;
					fired = true;
				} else {
					result = kronuz::journal::detail::execute_primitive(reader.io(), request);
					if (root_read && !fired && variant == 4) {
						auto changed = ownership;
						changed.image_checksum[0] ^= 1;
						write(lease_path, l::encode_generation_ownership(changed));
						fired = true;
					} else if (root_read && !fired && variant == 5) {
						const auto replacement = directory / "replacement.image";
						write(replacement, original);
						std::filesystem::rename(replacement, image_path);
						fired = true;
					}
				}
				c::Result stale;
				stale.token = result.token;
				++stale.token.step;
				require(!reader.complete(std::move(stale)) && reader.in_flight());
				require(reader.complete(std::move(result)));
			}
			if (variant < 2 || variant == 7) {
				require(reader.ready() && !reader.error() && reader.descriptor() == image.descriptor());
				require(bool(reader.certificate()) == (variant == 0));
				const int fd = ::open(lease_path.c_str(), O_RDWR | O_CLOEXEC);
				require(fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) != 0 &&
						(errno == EWOULDBLOCK || errno == EAGAIN));
				require(::close(fd) == 0);
			} else {
				require(!reader.ready() && reader.error());
				if (variant >= 4)
					require(fired);
				if (variant == 6)
					require(reader.error() == injected);
			}
			reader.close();
			drive(reader);
			require(reader.done() && !reader.ready() && !reader.in_flight());
			const int fd = ::open(lease_path.c_str(), O_RDWR | O_CLOEXEC);
			require(fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0);
			require(::close(fd) == 0 && limits.mapping_account->used() == 0);
		}
		session->close();
	}
	domain.close().wait();
	require(account->used() == initial && limits.mapping_account->used() == 0);
}
