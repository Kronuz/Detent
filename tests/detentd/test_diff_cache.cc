// Proof for the composed-diff cache in route_get_snapshot (detentd.cc), now
// backed by generation_cache::Cache: repeated requests for the SAME
// base_version between two accepted updates only compose once, an accepted
// update invalidates the cache (so a request for the same base_version
// after a write recomposes exactly once more, not zero and not reusing the
// stale entry), and the configurable max_diff_history_ bound evicts
// entries at the configured size instead of the old hardcoded 64.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include <thread>

#include "detentd.hh"
#include "detent_http/http_client.hh"

#include <http_asio.h>

using namespace std::chrono_literals;

namespace {

std::filesystem::path temp_path() {
	static std::mt19937_64 rng{std::random_device{}()};
	return std::filesystem::temp_directory_path() / ("jk_diff_cache_" + std::to_string(rng()) + ".json");
}

unsigned short temp_port() {
	static std::mt19937_64 rng{std::random_device{}()};
	return static_cast<unsigned short>(23000 + (rng() % 6000));
}

void wait_for_server_ready(std::uint16_t port, std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	for (;;) {
		try {
			(void)detent_http::http_get("127.0.0.1", port, "/v1/health", 500ms);
			return;
		} catch (const std::exception&) {
			if (std::chrono::steady_clock::now() >= deadline) throw;
			std::this_thread::sleep_for(10ms);
		}
	}
}

detent_http::HttpResponse get_with_base_version(
	std::uint16_t port, std::uint64_t base_version, std::chrono::milliseconds timeout = 2s) {
	detent_http::HttpConnection conn("127.0.0.1", port, timeout);
	return conn.get("/v1/snapshot", std::nullopt, base_version);
}

} // namespace

int main() {
	auto persist = temp_path();
	detentd::Options opts;
	opts.persist_path = persist;
	opts.max_diff_history = 5; // small and explicit, not the old hardcoded 64
	detentd::Distributor app(opts);
	http::HttpAsioService service(app, 2, 4, 256);
	service.enable_conditional();
	auto port = temp_port();
	service.start(port);
	std::this_thread::sleep_for(50ms);
	wait_for_server_ready(port, 3s);

	// Seed v1, then 5 more single-knob updates (v2..v6) -- 5 step-diffs,
	// exactly at max_diff_history, so nothing has been evicted yet. Base
	// versions 1..5 are all retained; base_version 1 is the OLDEST still
	// in the window.
	auto put1 = detent_http::http_put(
		"127.0.0.1", port, "/v1/snapshot",
		R"({"schema_version":1,"version":1,"knobs":{"team:flag":{"type":"bool","default":true}}})", 2s);
	assert(put1.status == 200);
	for (std::uint64_t v = 2; v <= 6; ++v) {
		auto patch = detent_http::http_patch(
			"127.0.0.1", port, "/v1/snapshot",
			R"({"schema_version":1,"version":)" + std::to_string(v) +
				R"(,"knobs":{"team:flag":{"type":"bool","default":)" + (v % 2 == 0 ? "false" : "true") + R"(}}})",
			2s);
		assert(patch.status == 200);
	}
	assert(app.current_version() == 6);

	// Two requests for the SAME base_version (2, needing a 4-step compose:
	// 2->3->4->5->6) between any writes: both must be served as diffs with
	// IDENTICAL bytes, but only the FIRST should have actually composed --
	// diffs_composed() proves the cache, not just diffs_served() (which a
	// correct-but-uncached implementation would also satisfy).
	auto composed_before = app.diffs_composed();
	auto served_before = app.diffs_served();

	auto resp1 = get_with_base_version(port, 2);
	assert(resp1.status == 200);
	assert(resp1.is_patch);
	auto resp2 = get_with_base_version(port, 2);
	assert(resp2.status == 200);
	assert(resp2.is_patch);
	assert(resp1.body == resp2.body);

	assert(app.diffs_served() == served_before + 2); // both answered as diffs
	assert(app.diffs_composed() == composed_before + 1); // only the first actually composed
	std::puts("two requests for the same base_version: one compose, two identical diffs served OK");

	// A new accepted update invalidates the cache -- the SAME base_version
	// (2) must recompose exactly once more (not reuse the now-stale entry,
	// and not fail to cache the new one either).
	auto patch7 = detent_http::http_patch(
		"127.0.0.1", port, "/v1/snapshot",
		R"({"schema_version":1,"version":7,"knobs":{"team:flag":{"type":"bool","default":true}}})", 2s);
	assert(patch7.status == 200);

	auto composed_before_2 = app.diffs_composed();
	auto resp3 = get_with_base_version(port, 2);
	assert(resp3.status == 200);
	assert(resp3.is_patch);
	assert(resp3.body != resp1.body); // genuinely a different (longer) diff now: 2->3->4->5->6->7
	assert(app.diffs_composed() == composed_before_2 + 1);

	auto resp4 = get_with_base_version(port, 2);
	assert(resp4.body == resp3.body);
	assert(app.diffs_composed() == composed_before_2 + 1); // second request reused the NEW cached entry
	std::puts("an accepted update invalidates the cache: recomposes once, then caches again OK");

	// base_version 1 was the oldest in the 5-entry window before this
	// round's write; after 7 total updates with max_diff_history=5, the
	// retained window is base_versions {3,4,5,6,7} -- 1 and 2 are evicted,
	// so a request for base_version 1 must fall back to the full body, not
	// a diff, proving max_diff_history_ is actually the configured 5, not
	// the old hardcoded 64 (which would still have room for it).
	auto resp_evicted = get_with_base_version(port, 1);
	assert(resp_evicted.status == 200);
	assert(!resp_evicted.is_patch);
	std::puts("a base_version outside the configured max_diff_history falls back to the full body OK");

	service.stop();
	std::filesystem::remove(persist);
	return 0;
}
