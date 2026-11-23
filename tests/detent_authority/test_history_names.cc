// History file names (S5.3, F6): every name history writes is a `history_name`, a peer's
// fetch of any other name is refused before the disk, a namespace holding `/` or 199 bytes
// still names a file, and a file above the fetch bound is refused unread. `--measure` times
// serving one file of the lane's default bound (64 MiB) from the page cache.
#include "history.hh"
#include <cassert>
#include <chrono>
#include <completion/posix.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

using namespace detent::authority;
namespace h = detent::authority::history;

int main(int argc, char **argv) {
	Identity incarnation{};
	incarnation[0] = 1;
	incarnation[15] = static_cast<char>(0xff);
	std::string longest(199, 'n');
	longest[7] = '/';
	const std::vector<std::string> spaces{"a", "team/app", "../up", "a-b.c_d", longest};
	const auto top = std::numeric_limits<std::uint64_t>::max();
	for (const auto &space : spaces) {
		assert(h::history_name(h::block_name(space, incarnation, 1, top)));
		assert(h::history_name(h::base_name(space, incarnation, 0)));
		assert(h::block_name(space, incarnation, 1, top).size() < 128);
	}
	assert(h::history_name(h::seal_name(1)) && h::history_name(h::seal_name(top)));
	assert(h::history_name(h::table_name));
	const auto token = h::namespace_token("a"), owner = h::hex(incarnation);
	assert(h::namespace_token("a") != h::namespace_token("b"));
	for (std::string bad : {std::string(), std::string("layout/"), std::string("../layout"), std::string(".tmp-layout"),
							std::string("notes"), std::string("seal-"), std::string("seal-000000000000000g"),
							std::string("seal-00000000000000001"), std::string("SEAL-0000000000000001"),
							"block-" + token + "-" + owner + "-01-2", "block-" + token + "-" + owner + "-1",
							"block-" + token + "-" + owner + "-1-2-3", "block-" + token + "-" + owner + "--2",
							"block-a-" + owner + "-1-2", "base-" + token + "-" + owner + "-18446744073709551616",
							"base-" + token + "-" + owner + "-1/../x", "base-" + token + "-" + owner + "-1-2",
							"BLOCK-" + token + "-" + owner + "-1-2", std::string("base-") + std::string(1, '\0')})
		assert(!h::history_name(bad));

	auto root = std::filesystem::current_path() / ".scratch" / ("history-names-" + std::to_string(getpid()));
	auto directory = root / "history";
	std::filesystem::create_directories(directory);
	assert(::chmod(directory.c_str(), 0700) == 0);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};
	auto io = std::make_shared<kronuz::journal::PosixIO>(directory);
	h::Directory files(io, {});
	// A namespace's files name it by token, so `/` and 199 bytes are a plain file name.
	for (const auto &space : spaces) {
		const auto name = h::block_name(space, incarnation, 1, 2);
		files.publish({{name, "block of " + space}});
		assert(files.find(name) == "block of " + space);
		assert(h::serve(*io, name, 1 << 20) == "block of " + space);
	}
	// Files history never writes are not served, though they are on disk.
	for (const auto &[path, name] : {std::pair{directory / "notes", std::string("notes")},
									 std::pair{directory / ".tmp-layout", std::string(".tmp-layout")},
									 std::pair{root / "secret", std::string("../secret")}}) {
		std::ofstream(path) << "not history";
		assert(!h::serve(*io, name, 1 << 20));
	}
	assert(!h::serve(*io, h::table_name, 1 << 20));
	files.publish({{std::string(h::table_name), std::string(4096, 't')}});
	assert(h::serve(*io, h::table_name, 4096) == std::string(4096, 't'));
	bool refused = false;
	try {
		h::serve(*io, h::table_name, 4095);
	} catch (const std::length_error &) {
		refused = true;
	}
	assert(refused);

	if (argc == 2 && std::string_view(argv[1]) == "--measure") {
		const std::size_t bound = 64 * 1024 * 1024;
		const auto name = h::base_name("measure", incarnation, 1);
		files.publish({{name, std::string(bound, 'm')}});
		double slowest = 0, total = 0;
		for (unsigned run = 0; run != 8; ++run) {
			const auto started = std::chrono::steady_clock::now();
			auto bytes = h::serve(*io, name, bound);
			const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
			assert(bytes && bytes->size() == bound);
			slowest = std::max(slowest, ms);
			total += ms;
		}
		std::cout << "serve 64 MiB from the page cache: mean " << total / 8 << " ms, slowest " << slowest << " ms\n";
	}
	std::cout << "history names: " << spaces.size() << " namespaces name files, other names refused\n";
}
