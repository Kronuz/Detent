// Section 7/9/13: the file-source updater. Valid atomic replacement produces
// exactly one accepted swap; malformed, stale, and partial updates never
// change state; reads continue through every injected failure.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <random>
#include <thread>

#include "detent/client.hh"
#include "detent/detail/file.hh"

using namespace detent;
using namespace std::chrono_literals;

namespace {

std::filesystem::path temp_path(std::string_view suffix) {
	static std::mt19937_64 rng{std::random_device{}()};
	return std::filesystem::temp_directory_path() /
	       ("detent_test_" + std::to_string(rng()) + std::string(suffix));
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
	// Atomic-rename-over-target, matching the publisher contract the file
	// source assumes (Section 10): write a sibling temp file, then rename.
	auto tmp = path;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary);
		out << contents;
	}
	std::filesystem::rename(tmp, path);
}

bool wait_until(std::function<bool()> pred, std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (pred()) return true;
		std::this_thread::sleep_for(10ms);
	}
	return pred();
}

constexpr std::string_view kDefaults = R"({
	"schema_version": 1,
	"knobs": {
		"checkout:killswitch_new_path": { "type": "bool", "default": false },
		"search:max_results": { "type": "int", "default": 25 }
	}
})";

} // namespace

int main() {
	for (auto interval : {0ms, -1ms}) {
		InitOptions invalid;
		invalid.poll_interval = interval;
		invalid.source_poll = [](std::uint64_t) { return std::optional<InitOptions::SourcePayload>{}; };
		bool rejected = false;
		try { Client client(std::move(invalid)); }
		catch (const std::invalid_argument&) { rejected = true; }
		assert(rejected);
	}

	auto defaults_path = temp_path("_defaults.json");
	auto source_path = temp_path("_source.json");
	write_file(defaults_path, kDefaults);
	assert(detail::read_file(defaults_path, kDefaults.size()) == kDefaults);
	bool oversized = false;
	try { (void)detail::read_file(defaults_path, kDefaults.size() - 1); }
	catch (const std::runtime_error&) { oversized = true; }
	assert(oversized);
	assert(detail::read_file(defaults_path, 16384) == kDefaults);

	InitOptions opts;
	opts.defaults_path = defaults_path;
	opts.source_path = source_path;
	opts.poll_interval = 20ms;

	// Section 9: "Source unavailable at startup -> Serve version-0 defaults;
	// log and increment refresh failure" -- source_path is configured but the
	// file does not exist yet.
	Client c(std::move(opts));
	assert(c.get("search:max_results").value() == 25);
	assert(c.health().accepted_version == 0);

	// A valid versioned snapshot swaps in.
	write_file(source_path, R"({
		"schema_version": 1, "version": 1,
		"knobs": { "search:max_results": { "type": "int", "default": 500 } }
	})");
	bool swapped = wait_until([&] { return c.get("search:max_results").value() == 500; }, 2000ms);
	assert(swapped);
	assert(c.health().accepted_version == 1);
	assert(c.health().source_connected);
	// Source omitted the bool knob -> baked default retained.
	assert(c.eval("checkout:killswitch_new_path").value() == false);

	// A stale/duplicate version is ignored; last-known-good is untouched.
	write_file(source_path, R"({
		"schema_version": 1, "version": 1,
		"knobs": { "search:max_results": { "type": "int", "default": 1 } }
	})");
	std::this_thread::sleep_for(200ms);
	assert(c.get("search:max_results").value() == 500);
	assert(c.metrics().stale_updates.load() >= 1);

	// A malformed update is rejected wholesale; last-known-good is untouched.
	write_file(source_path, "{not valid json");
	std::this_thread::sleep_for(200ms);
	assert(c.get("search:max_results").value() == 500);
	assert(c.metrics().refresh_failures.load() >= 1);

	// A later, higher, valid version still applies after prior rejections.
	write_file(source_path, R"({
		"schema_version": 1, "version": 2,
		"knobs": { "search:max_results": { "type": "int", "default": 7 } }
	})");
	bool swapped_again = wait_until([&] { return c.get("search:max_results").value() == 7; }, 2000ms);
	assert(swapped_again);
	assert(c.health().accepted_version == 2);
	assert(c.metrics().snapshot_swaps.load() == 2);

	// A publisher that reuses the exact previous mtime across an atomic
	// rename (e.g. a tool that preserves timestamps, or a coarse-resolution
	// filesystem) must still be observed -- previously this could be missed
	// forever, since the updater only re-read on an mtime change.
	auto reused_mtime = std::filesystem::last_write_time(source_path);
	write_file(source_path, R"({
		"schema_version": 1, "version": 3,
		"knobs": { "search:max_results": { "type": "int", "default": 999 } }
	})");
	std::filesystem::last_write_time(source_path, reused_mtime); // force the same mtime as v2
	bool swapped_same_mtime = wait_until([&] { return c.get("search:max_results").value() == 999; }, 2000ms);
	assert(swapped_same_mtime);
	assert(c.health().accepted_version == 3);

	InitOptions source_only;
	source_only.source_path = source_path;
	source_only.poll_interval = 20ms;
	Client without_defaults(std::move(source_only));
	assert(wait_until([&] { return without_defaults.health().accepted_version == 3; }, 2000ms));
	assert(without_defaults.get("search:max_results").value() == 999);
	assert(!without_defaults.eval("checkout:killswitch_new_path"));
	write_file(source_path, R"({"schema_version":1,"version":4,"knobs":{}})");
	assert(wait_until([&] { return without_defaults.health().accepted_version == 4; }, 2000ms));
	assert(!without_defaults.get("search:max_results"));
	Client empty(InitOptions{});
	assert(empty.health().initialized);
	assert(!empty.get("search:max_results"));

	std::filesystem::remove(defaults_path);
	std::filesystem::remove(source_path);

	std::puts("file source updater (valid/stale/malformed/recovery/same-mtime) OK");
	return 0;
}
