// A voter started on a directory of another storage format refuses it with a named error and
// never fences and waits (docs/mechanisms/format-refusal.md).
#include "service.hh"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

using namespace detent::authority;
namespace {
namespace fs = std::filesystem;
void require(bool condition, const std::string &message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
		std::_Exit(1);
	}
}
ServiceOptions options(const fs::path &directory) {
	ServiceOptions o;
	o.directory = directory;
	o.node.context.cluster[0] = 'c';
	o.node.context.group[0] = 'g';
	o.node.context.epoch[0] = 'e';
	o.node.configuration.cluster = o.node.context.cluster;
	o.node.configuration.configuration = o.node.context.group;
	o.node.configuration.local = 1;
	o.node.configuration.voters = {1};
	o.node.timing = {.heartbeat = 20, .rpc_timeout = 40, .election_min = 100, .election_max = 200};
	o.credentials.push_back({"0123456789abcdef0123456789abcdef", "writer", {"ns"}});
	o.timeout = std::chrono::milliseconds(3000);
	return o;
}
// Constructs a service under a watchdog: a voter that fences and waits instead of settling its
// start ends the process. Returns what() when the start is refused, empty when it started.
std::string start(const fs::path &directory) {
	std::atomic<bool> settled{false};
	std::thread watchdog([&] {
		for (int i = 0; i < 200 && !settled; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if (!settled) {
			std::cerr << "FAIL: the voter neither started nor refused within 20 s\n";
			std::_Exit(3);
		}
	});
	std::string refusal;
	try {
		Service service(options(directory));
	} catch (const std::exception &error) {
		refusal = error.what();
	}
	settled = true;
	watchdog.join();
	return refusal;
}
void stamp(const fs::path &history, const std::string &text) {
	std::ofstream(history / "format", std::ios::binary | std::ios::trunc) << text;
}
bool has(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }
} // namespace
int main() {
	auto directory = fs::current_path() / ".scratch" / ("authority-format-" + std::to_string(getpid()));
	auto history = fs::path(directory.string() + ".history");
	fs::create_directories(directory.parent_path());
	fs::remove_all(directory);
	fs::remove_all(history);

	// A new directory is stamped and its journal recovers on restart.
	require(start(directory).empty(), "a new directory starts");
	std::string current;
	{
		std::ifstream file(history / "format", std::ios::binary);
		current.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}
	require(current == "detent authority storage format 2\n", "a new directory is stamped, got '" + current + "'");
	require(start(directory).empty(), "a journal of the current format recovers");

	// A journal written before the stamp (format 1) is refused naming both formats.
	fs::remove(history / "format");
	auto refused = start(directory);
	require(has(refused, "storage format 1") && has(refused, "storage format 2") && !has(refused, "fenced"),
			"an unstamped journal is refused naming both formats, got '" + refused + "'");

	// Another stamp, older or newer, or an unreadable one, is refused the same way.
	stamp(history, "detent authority storage format 1\n");
	refused = start(directory);
	require(has(refused, "storage format 1") && has(refused, "storage format 2"),
			"format 1 is refused naming both, got '" + refused + "'");
	stamp(history, "detent authority storage format 9\n");
	refused = start(directory);
	require(has(refused, "storage format 9") && has(refused, "storage format 2"),
			"format 9 is refused naming both, got '" + refused + "'");
	stamp(history, "garbage");
	refused = start(directory);
	require(has(refused, "unreadable format stamp") && has(refused, "storage format 2"),
			"an unreadable stamp is refused, got '" + refused + "'");

	// The refusal changed nothing: the stamp restored, the journal recovers.
	stamp(history, current);
	require(start(directory).empty(), "the journal recovers once its stamp is whole");

	// A failure before admission settles the start with its error, never a hang.
	{
		std::ofstream manifest(directory / "manifest", std::ios::binary | std::ios::trunc);
		manifest << "not a manifest";
	}
	require(!start(directory).empty(), "an unreadable manifest refuses the start");

	fs::remove_all(directory);
	fs::remove_all(history);
	std::cout << "format refusal passed\n";
	return 0;
}
