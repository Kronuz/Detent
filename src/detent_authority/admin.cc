// Explicit offline migration. Authority and legacy stores are never mixed.
#include "admin_runtime.hh"
#include "detent/json_schema.hh"
#include <array>
#include <chrono>
#include <completion/posix.h>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <random>
#include <sys/stat.h>
#include <unistd.h>
using namespace detent::authority;
using namespace kronuz::consensus;
namespace {
void check(bool value, const char *message) {
	if (!value)
		throw std::runtime_error(message);
}
std::string input(const std::filesystem::path &path, std::size_t maximum) {
	std::ifstream stream(path, std::ios::binary);
	check(bool(stream), "cannot open import");
	std::string bytes;
	std::array<char, 65536> chunk;
	while (stream) {
		stream.read(chunk.data(), chunk.size());
		auto count = static_cast<std::size_t>(stream.gcount());
		check(count <= maximum - bytes.size(), "import byte bound");
		bytes.append(chunk.data(), count);
	}
	check(stream.eof(), "import read failure");
	return bytes;
}
void sync_parent(const std::filesystem::path &path) {
	auto parent = path.parent_path();
	if (parent.empty())
		parent = ".";
	int fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	check(fd >= 0, "cannot open parent directory");
	auto result = fsync(fd);
	close(fd);
	check(result == 0, "parent directory durability");
}
void output(const std::filesystem::path &path, std::string_view bytes) {
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	check(fd >= 0, "export output must not exist");
	struct Close {
		int fd;
		~Close() { close(fd); }
	} close_fd{fd};
	while (!bytes.empty()) {
		auto n = write(fd, bytes.data(), bytes.size());
		check(n > 0, "export write failure");
		bytes.remove_prefix(n);
	}
	check(fsync(fd) == 0, "export durability");
	sync_parent(path);
}

} // namespace
int main(int argc, char **argv) {
	try {
		check(argc >= 4, "usage: detent-authority-admin import CONFIG SNAPSHOT | "
						 "export CONFIG OUTPUT LEGACY_VERSION");
		auto options = load_service_options(argv[2]);
		if (std::string_view(argv[1]) == "import") {
			check(argc == 4, "import requires CONFIG SNAPSHOT");
			auto bytes = input(argv[3], options.node.application.image_bytes);
			auto parsed = detent::parse_source_snapshot(
				bytes, "offline-import", options.node.application.keys, options.node.application.image_bytes);
			check(bool(parsed), "invalid legacy snapshot");
			auto snapshot = std::move(parsed).value();
			bytes.clear();
			bytes.shrink_to_fit();
			check(std::filesystem::create_directory(options.directory),
				  "import requires a new authority directory");
			check(chmod(options.directory.c_str(), 0700) == 0, "authority directory permissions");
			sync_parent(options.directory);
			admin::Runtime runtime(options);
			std::random_device random;
			Identity storage{};
			for (auto &byte : storage)
				byte = static_cast<char>(random());
			check(storage != Identity{}, "invalid storage identity");
			runtime.node.create(storage);
			runtime.ready();
			Command command;
			command.context = options.node.context;
			command.producer = "detent-offline-import";
			command.kind = Kind::Open;
			command.nonce = storage;
			check(runtime.submit(command).status == Status::Applied, "import session rejected");
			command.kind = Kind::Transaction;
			command.nonce = {};
			command.generation = 1;
			command.sequence = 1;
			auto flush = [&] {
				if (command.mutations.empty())
					return;
				check(runtime.submit(command).status == Status::Applied, "import transaction rejected");
				command.mutations.clear();
				++command.sequence;
			};
			for (const auto &[key, value] : snapshot.knobs) {
				auto ns = key.substr(0, key.find(':'));
				if (ns != command.name_space) {
					flush();
					command.name_space = ns;
				}
				command.mutations.push_back({key, value, {}, {}});
				bool fits = true;
				try {
					encode(command, options.node.application);
				} catch (const std::invalid_argument &) {
					fits = false;
				}
				if (!fits) {
					auto last = std::move(command.mutations.back());
					command.mutations.pop_back();
					check(!command.mutations.empty(), "import definition exceeds request bound");
					flush();
					command.mutations.push_back(std::move(last));
				}
				if (command.mutations.size() == options.node.application.mutations)
					flush();
			}
			flush();
			check(runtime.node.state().size() == snapshot.knobs.size(), "import count mismatch");
			runtime.node.state().for_each([&](const std::string &key, const Record &record) {
				auto found = snapshot.knobs.find(key);
				check(found != snapshot.knobs.end() && found->second == *record.value,
					  "import semantic mismatch");
			});
			runtime.checkpoint();
			std::printf("Imported %zu knobs into a separate authority; legacy "
						"version was not reused.\n",
						snapshot.knobs.size());
		} else if (std::string_view(argv[1]) == "export") {
			check(argc == 5, "export requires an explicit positive legacy version");
			std::string version(argv[4]);
			std::size_t used = 0;
			auto value = std::stoull(version, &used);
			check(used == version.size() && !version.empty() && version[0] != '-' && value > 0,
				  "invalid legacy version");
			admin::Runtime runtime(options);
			runtime.recover_committed();
			auto snapshot = runtime.node.state().snapshot();
			snapshot.version = value;
			output(argv[3], detent::serialize_snapshot(snapshot));
			std::printf("Exported %zu knobs; publish only with a version above the "
						"destination's last accepted version.\n",
						snapshot.knobs.size());
		} else
			throw std::runtime_error("unknown migration command");
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
