#include "detent_local/ownership.hh"
#include "snapshot_fixture.hh"
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace l = detent::local;
namespace {
void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!bytes.empty())
		stream.write(bytes.data(), bytes.size());
	stream.close();
	require(static_cast<bool>(stream));
	std::filesystem::permissions(path,
								 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
void rejects(const std::filesystem::path &directory) {
	bool rejected = false;
	try {
		l::open_generation(directory);
	} catch (const std::exception &) {
		rejected = true;
	}
	require(rejected);
}
} // namespace
int main() {
	try {
		auto directory =
			std::filesystem::current_path() / ".scratch" / ("managed-reader-" + std::to_string(::getpid()));
		require(std::filesystem::create_directories(directory));
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
		l::GenerationId generation;
		generation.fill('g');
		l::ImageBuilder builder(source, generation);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		auto descriptor = builder.descriptor();
		auto name = l::generation_name(generation);
		auto image = builder.bytes();
		write(directory / (name + ".image"), {image->data(), image->size()});
		write(directory / "selected", l::encode_descriptor(descriptor));
		write(directory / (name + ".lease"), {});
		require(l::open_generation(directory).cursor() == cursor()); // Existing empty leases remain readable.
		l::StorageIdentity storage;
		storage.fill('s');
		l::GenerationOwnership ownership{storage, generation, descriptor.length, descriptor.checksum};
		auto lease = l::encode_generation_ownership(ownership);
		write(directory / (name + ".lease"), lease);
		rejects(directory); // Managed leases require a trusted storage identity.
		write(directory / "storage.meta", l::encode_storage_identity(storage));
		require(l::open_generation(directory).cursor() == cursor());
		for (unsigned field = 0; field < 4; ++field) {
			auto wrong = ownership;
			if (field == 0)
				wrong.storage[0] ^= 1;
			if (field == 1)
				wrong.generation[0] ^= 1;
			if (field == 2)
				++wrong.image_length;
			if (field == 3)
				wrong.image_checksum[0] ^= 1;
			write(directory / (name + ".lease"), l::encode_generation_ownership(wrong));
			rejects(directory);
		}
		write(directory / (name + ".lease"), lease);
		for (auto length : {0u, 63u}) {
			auto record = l::encode_storage_identity(storage);
			write(directory / "storage.meta", std::span(record).first(length));
			rejects(directory);
		}
		auto broken_storage = l::encode_storage_identity(storage);
		broken_storage.back() ^= 1;
		write(directory / "storage.meta", broken_storage);
		rejects(directory);
		write(directory / "storage.meta", l::encode_storage_identity(storage));
		std::filesystem::permissions(directory / "storage.meta", std::filesystem::perms::others_write,
									 std::filesystem::perm_options::add);
		rejects(directory);
		write(directory / "storage.meta", l::encode_storage_identity(storage));
		std::filesystem::create_hard_link(directory / "storage.meta", directory / "foreign-meta");
		rejects(directory);
		std::filesystem::remove(directory / "foreign-meta");
		std::filesystem::rename(directory / "storage.meta", directory / "real-meta");
		std::filesystem::create_symlink("real-meta", directory / "storage.meta");
		rejects(directory);
		std::filesystem::remove(directory / "storage.meta");
		std::filesystem::rename(directory / "real-meta", directory / "storage.meta");
		for (auto length : {1u, 127u}) {
			write(directory / (name + ".lease"), std::span(lease).first(length));
			rejects(directory);
		}
		lease.back() ^= 1;
		write(directory / (name + ".lease"), lease);
		rejects(directory);
		lease.back() ^= 1;
		write(directory / (name + ".lease"), lease);
		require(l::open_generation(directory).cursor() == cursor());
		write(directory / (name + ".lease"), {});
		std::filesystem::remove(directory / "storage.meta");
		require(l::open_generation(directory).cursor() == cursor());
		std::filesystem::remove_all(directory);
		std::cout << "dual legacy/managed reader, selection binding and unsafe identity rejection passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
