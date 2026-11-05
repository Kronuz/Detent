#include "detent/reader.h"
#include "detent_testing/generation_writer.hh"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace {
void check(bool value) {
	if (!value)
		std::abort();
}
fs::path entry(const fs::path &store, std::string_view kind) {
	if (kind == "change" || kind == "selected")
		return store / kind;
	for (const auto &file : fs::directory_iterator(store))
		if (file.path().extension() == kind)
			return file.path();
	std::abort();
}
void reshape(const fs::path &path, std::string_view shape) {
	if (shape == "regular")
		return;
	if (shape == "mode") {
		check(::chmod(path.c_str(), 0666) == 0);
		return;
	}
	if (shape == "short") {
		check(::truncate(path.c_str(), 7) == 0);
		return;
	}
	const auto saved = path.string() + ".saved";
	fs::rename(path, saved);
	if (shape == "fifo")
		check(::mkfifo(path.c_str(), 0600) == 0);
	else if (shape == "directory")
		fs::create_directory(path);
	else if (shape == "symlink")
		fs::create_symlink(saved, path);
	else if (shape == "hardlink") {
		fs::create_hard_link(saved, path);
	} else
		std::abort();
}
}
int main() {
	const auto root = fs::current_path() / ".scratch" / ("reader-files-" + std::to_string(::getpid()));
	fs::create_directories(root);
	unsigned failures = 0;
	for (const auto kind : {"change", "selected", ".lease", ".pack"}) {
		for (const auto shape : {"regular", "fifo", "directory", "symlink", "hardlink", "mode", "short"}) {
			const auto store = root / (std::string(kind) + "-" + shape);
			{
				detent::testing::GenerationWriter writer(store);
				check(writer.publish_json(R"({"schema_version":1,"knobs":{"t:rev":{"type":"int","default":7}}})") == 1);
			}
			reshape(entry(store, kind), shape);
			const auto child = ::fork();
			check(child >= 0);
			if (child == 0) {
				::alarm(2);
				const auto path = store.string();
				detent_open_options options{sizeof options, {path.data(), path.size()}, nullptr, 0, uint32_t(::getuid())};
				detent_reader *reader = nullptr;
				auto status = detent_open(&options, &reader);
				std::int64_t value = -7;
				if (status == DETENT_OK)
					status = detent_get(reader, {"t:rev", 5}, -7, &value);
				const bool positive = std::string_view(shape) == "regular";
				const bool valid = positive ? status == DETENT_OK && value == 7
				                            : status == DETENT_SOURCE_FAILURE && value == -7;
				detent_close(reader);
				std::_Exit(valid ? 0 : 1);
			}
			int status = 0;
			check(::waitpid(child, &status, 0) == child);
			const bool passed = WIFEXITED(status) && WEXITSTATUS(status) == 0;
			std::printf("%s %s: %s (signal %d)\n", kind, shape, passed ? "pass" : "FAIL",
			            WIFSIGNALED(status) ? WTERMSIG(status) : 0);
			failures += !passed;
		}
	}
	fs::remove_all(root);
	return failures ? 1 : 0;
}
