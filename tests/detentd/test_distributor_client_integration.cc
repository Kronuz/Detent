// End-to-end proof: a real detent::Client, configured with
// detent_http::make_http_poll_source, observes a snapshot published to a
// real Distributor via HTTP PUT -- publisher -> distributor -> client.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <thread>

#include "detentd.hh"
#include "detent/client.hh"
#include "detent_http/http_client.hh"
#include "detent_http/http_source.hh"

#include <http_asio.h>

using namespace std::chrono_literals;

namespace {

std::filesystem::path temp_path(std::string_view suffix) {
	static std::mt19937_64 rng{std::random_device{}()};
	return std::filesystem::temp_directory_path() / ("jk_dist_client_" + std::to_string(rng()) + std::string(suffix));
}

unsigned short temp_port() {
	static std::mt19937_64 rng{std::random_device{}()};
	return static_cast<unsigned short>(21000 + (rng() % 10000));
}

bool wait_until(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (pred()) return true;
		std::this_thread::sleep_for(20ms);
	}
	return pred();
}

// A fixed sleep_for(N) between service.start() and the first client request
// is not a portable "the reactor thread has bound and is accepting" bound --
// under heavy scheduler contention (e.g. 12 sanitizer-instrumented test
// binaries sharing 4 cores in CI) it can be too short, and an unguarded
// connect() failure here is a *test-harness* bug: it throws out of main()
// uncaught -> std::terminate() -> a spurious "Subprocess aborted", not a
// Detent library defect. Retry instead of trusting the sleep.
void wait_for_server_ready(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	for (;;) {
		try {
			(void)detent_http::http_get(host, port, "/v1/health", 500ms);
			return;
		} catch (const std::exception&) {
			if (std::chrono::steady_clock::now() >= deadline) throw;
			std::this_thread::sleep_for(10ms);
		}
	}
}

} // namespace

int main() {
	auto persist = temp_path(".json");
	detentd::Options opts;
	opts.persist_path = persist;
	detentd::Distributor app(opts);
	http::HttpAsioService service(app, 2, 4, 256);
	service.enable_conditional();
	auto port = temp_port();
	service.start(port);
	std::this_thread::sleep_for(50ms);
	wait_for_server_ready("127.0.0.1", port, 3s);

	auto defaults_path = temp_path("_defaults.json");
	{
		std::ofstream out(defaults_path, std::ios::binary);
		out << R"({
			"schema_version": 1,
			"knobs": {
				"checkout:killswitch_new_path": { "type": "bool", "default": false },
				"search:max_results": { "type": "int", "default": 25 }
			}
		})";
	}

	detent::InitOptions client_opts;
	client_opts.defaults_path = defaults_path;
	client_opts.source_poll = detent_http::make_http_poll_source("127.0.0.1", port, "/v1/snapshot", 2s);
	client_opts.poll_interval = 50ms;

	detent::Client client(std::move(client_opts));

	// Before any publish: baked defaults.
	assert(client.get("search:max_results").value() == 25);

	// Publish a new version to the distributor; the client's poll source
	// should pick it up within a few poll intervals.
	auto put = detent_http::http_put(
		"127.0.0.1", port, "/v1/snapshot",
		R"({"schema_version":1,"version":1,"knobs":{"search:max_results":{"type":"int","default":500}}})", 2s);
	assert(put.status == 200);

	bool updated = wait_until([&] { return client.get("search:max_results").value() == 500; }, 3s);
	assert(updated);
	assert(client.health().accepted_version == 1);
	assert(client.health().source_connected);
	// Source omitted the bool knob -> baked default retained.
	assert(client.eval("checkout:killswitch_new_path").value() == false);

	// PATCH a single knob (v2): the client's next poll should pick this up
	// too, and -- because its own last-accepted version (1) is exactly what
	// the distributor's cached diff was computed against -- should be
	// served the small diff, not the full body (see detentd.hh's
	// current_diff_bytes_ comment). diffs_served() is the test-visible
	// proof the optimization actually engaged, not just that the value
	// happened to update correctly (which a full-body fallback would also
	// produce).
	auto diffs_before = app.diffs_served();
	auto patch = detent_http::http_patch(
		"127.0.0.1", port, "/v1/snapshot",
		R"({"schema_version":1,"version":2,"knobs":{"checkout:killswitch_new_path":{"type":"bool","default":true}}})",
		2s);
	assert(patch.status == 200);

	bool patched = wait_until([&] { return client.eval("checkout:killswitch_new_path").value() == true; }, 3s);
	assert(patched);
	assert(client.health().accepted_version == 2);
	// The knob the patch never mentioned is untouched.
	assert(client.get("search:max_results").value() == 500);
	bool got_diff = wait_until([&] { return app.diffs_served() > diffs_before; }, 3s);
	assert(got_diff);

	service.stop();
	std::filesystem::remove(persist);
	std::filesystem::remove(defaults_path);

	std::puts("distributor + HTTP poll source + Client end-to-end OK");
	return 0;
}
