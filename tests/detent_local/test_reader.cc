#include "detent_local/reader.hh"
#include "snapshot_fixture.hh"
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace l = detent::local;
namespace {
void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream.write(bytes.data(), bytes.size());
	stream.close();
	require(static_cast<bool>(stream));
	std::filesystem::permissions(path,
								 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
} // namespace
int main() {
	try {
		auto directory = std::filesystem::current_path() / ".scratch" /
						 ("generation-reader-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
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
		write(directory / (name + ".lease"), {});
		write(directory / "selected", l::encode_descriptor(descriptor));
		l::GenerationReader reader(directory, {}, cursor());
		require(!reader.read_view());
		require(reader.refresh());
		require(!reader.refresh());
		auto pinned = reader.read_view();
		require(pinned.cursor() == cursor());
		auto foreign = cursor();
		foreign.scope[0] ^= 1;
		l::GenerationReader wrong(directory, {}, foreign);
		bool foreign_rejected = false;
		try {
			wrong.refresh();
		} catch (const std::exception &) {
			foreign_rejected = true;
		}
		require(foreign_rejected && !wrong.read_view());
		std::jthread reads([&](std::stop_token stop) {
			while (!stop.stop_requested()) {
				auto view = reader.read_view();
				require(view && view.cursor() == cursor());
			}
		});
		for (unsigned i = 0; i != 10; ++i)
			require(!reader.refresh());
		reads.request_stop();
		reads.join();
		write(directory / "selected", {});
		bool rejected = false;
		try {
			reader.refresh();
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		require(reader.read_view().cursor() == cursor());
		require(reader.descriptor() == descriptor);
		// Acquisition remains usable even when all directory paths disappear.
		std::filesystem::remove_all(directory);
		require(reader.read_view().cursor() == pinned.cursor());
		std::cout << "worker refresh, concurrent root acquisition and failed-refresh retention passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
