#include "snapshot_fixture.hh"
#include "trusted_records.hh"
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
		const auto built = trusted_records::build(source);
		const auto descriptor = built.descriptor();
		const auto name = l::generation_name(descriptor.generation);
		trusted_records::install(directory, built);
		require(l::open_generation(directory).cursor() == local_cursor());
		const auto table = trusted_records::slurp(directory / "selected");
		// The `.lease` is an empty regular file, singly linked: a reader locks it and nothing else.
		write(directory / (name + ".lease"), std::string_view("x"));
		rejects(directory);
		std::filesystem::remove(directory / (name + ".lease"));
		rejects(directory);
		write(directory / "other", {});
		std::filesystem::create_symlink("other", directory / (name + ".lease"));
		rejects(directory);
		std::filesystem::remove(directory / (name + ".lease"));
		std::filesystem::create_hard_link(directory / "other", directory / (name + ".lease"));
		rejects(directory);
		std::filesystem::remove(directory / (name + ".lease"));
		std::filesystem::remove(directory / "other");
		write(directory / (name + ".lease"), {});
		require(l::open_generation(directory).cursor() == local_cursor());
		// The table: whole, trusted, and naming the image's cursor and profile.
		for (const auto at : {std::size_t(0), std::size_t(70), table.size() - 1}) {
			auto broken = table;
			broken[at] ^= 1;
			write(directory / "selected", broken);
			rejects(directory);
		}
		write(directory / "selected", std::span(table).first(table.size() - 1));
		rejects(directory);
		write(directory / "selected", table);
		std::filesystem::permissions(directory / "selected", std::filesystem::perms::others_write,
									 std::filesystem::perm_options::add);
		rejects(directory);
		write(directory / "selected", table);
		auto decoded = l::decode_table(table);
		auto other = decoded;
		other.profile.required_capabilities ^= 1;
		write(directory / "selected", l::encode_table(other));
		rejects(directory);
		other = decoded;
		++other.entries.front().cursor.position;
		write(directory / "selected", l::encode_table(other));
		rejects(directory);
		other = decoded;
		other.entries.push_back(other.entries.front());
		other.entries.back().name += "x";
		other.entries.back().slot = 1;
		write(directory / "selected", l::encode_table(other));
		rejects(directory); // D88: the agent serves one namespace
		// D95: a flat image (`DTIMAGE1`) is refused like any other format, whole or as a root's framing.
		const auto image_path = directory / (name + ".image");
		const auto root = trusted_records::slurp(image_path);
		for (const std::size_t length : {std::size_t(192), root.size()}) {
			auto flat = root;
			flat.resize(length);
			std::copy_n("DTIMAGE1", 8, flat.begin());
			write(image_path, flat);
			other = decoded;
			other.entries.front().root_length = flat.size();
			other.entries.front().root = trusted_records::sha(flat);
			write(directory / "selected", l::encode_table(other));
			rejects(directory);
		}
		write(image_path, root);
		write(directory / "selected", table);
		require(l::open_generation(directory).cursor() == local_cursor());
		std::filesystem::remove_all(directory);
		std::cout << "the table, lease and image binding of a reader, and unsafe entry rejection passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
