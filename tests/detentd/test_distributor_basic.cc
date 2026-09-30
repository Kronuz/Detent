// Distributor unit/integration tests: PUT/GET semantics, conditional 304,
// stale/malformed rejection, and restart-time persistence -- all in-process
// (no subprocess), using Distributor + http::HttpAsioService directly and
// detent_http's client for requests.
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
	return std::filesystem::temp_directory_path() / ("jk_dist_test_" + std::to_string(rng()) + ".json");
}

unsigned short temp_port() {
	static std::mt19937_64 rng{std::random_device{}()};
	return static_cast<unsigned short>(20000 + (rng() % 10000));
}

// See test_distributor_client_integration.cc's identical helper: a fixed
// sleep_for(50ms) is not a portable "the reactor has bound" bound under
// scheduler contention (many sanitizer-instrumented test binaries sharing
// few cores in CI), and an unguarded connect() failure right after
// RunningServer's constructor throws out of main() uncaught -> a spurious
// "Subprocess aborted", not a Detent library defect.
void wait_for_server_ready(unsigned short port, std::chrono::milliseconds timeout) {
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

struct RunningServer {
	detentd::Distributor app;
	http::HttpAsioService service;
	unsigned short port;

	explicit RunningServer(detentd::Options options)
		: app(std::move(options)), service(app, 2, 4, 256), port(temp_port()) {
		service.enable_conditional();
		service.start(port);
		std::this_thread::sleep_for(50ms); // let the reactor threads bind
		wait_for_server_ready(port, 3s);
	}
	~RunningServer() { service.stop(); }
};

void test_put_get_conditional_and_rejections() {
	auto persist = temp_path();
	detentd::Options opts;
	opts.persist_path = persist;
	RunningServer server(opts);

	// No snapshot yet.
	auto miss = detent_http::http_get("127.0.0.1", server.port, "/v1/snapshot", 2s);
	assert(miss.status == 404);

	// Accept a valid snapshot.
	std::string body = R"({"schema_version":1,"version":1,"knobs":{"a:b":{"type":"int","default":7}}})";
	auto put1 = detent_http::http_put("127.0.0.1", server.port, "/v1/snapshot", body, 2s);
	assert(put1.status == 200);

	auto get1 = detent_http::http_get("127.0.0.1", server.port, "/v1/snapshot", 2s);
	assert(get1.status == 200);
	assert(get1.body == body);
	assert(get1.etag.has_value());

	// Conditional GET with the ETag we just got: 304, no body.
	auto get2 = detent_http::http_get("127.0.0.1", server.port, "/v1/snapshot", 2s, get1.etag);
	assert(get2.status == 304);
	assert(get2.body.empty());

	// Stale/duplicate version rejected.
	auto put_stale = detent_http::http_put("127.0.0.1", server.port, "/v1/snapshot", body, 2s);
	assert(put_stale.status == 409);

	// Malformed JSON rejected.
	auto put_bad = detent_http::http_put("127.0.0.1", server.port, "/v1/snapshot", "not json", 2s);
	assert(put_bad.status == 400);

	// A newer version is accepted and visible.
	std::string body2 = R"({"schema_version":1,"version":2,"knobs":{"a:b":{"type":"int","default":9}}})";
	auto put2 = detent_http::http_put("127.0.0.1", server.port, "/v1/snapshot", body2, 2s);
	assert(put2.status == 200);
	auto get3 = detent_http::http_get("127.0.0.1", server.port, "/v1/snapshot", 2s);
	assert(get3.body == body2);

	auto health = detent_http::http_get("127.0.0.1", server.port, "/v1/health", 2s);
	assert(health.status == 200);

	std::filesystem::remove(persist);
	std::puts("distributor PUT/GET/conditional/rejections OK");
}

void test_oversized_put_is_rejected_without_oom() {
	// Confirms the on_request_body() size cap actually engages: the http
	// framework's default buffered-body path has no built-in limit (checked
	// by reading http_request_parser.h), so without this cap an oversized
	// Content-Length would buffer in full before json_schema's own size
	// check ever ran. A capped connection gets dropped (the BodySink API has
	// no polite-rejection signal), which http_put surfaces as a thrown
	// transport error, not a parsed HTTP response.
	auto persist = temp_path();
	detentd::Options opts;
	opts.persist_path = persist;
	RunningServer server(opts);

	std::string oversized(20u * 1024 * 1024, 'x'); // 20 MiB > the 16 MiB cap
	bool threw = false;
	try {
		(void)detent_http::http_put("127.0.0.1", server.port, "/v1/snapshot", oversized, 2s);
	} catch (const std::exception&) {
		threw = true;
	}
	assert(threw);

	// The server is still alive and unaffected: a normal PUT right after
	// still works.
	std::string body = R"({"schema_version":1,"version":1,"knobs":{}})";
	auto put = detent_http::http_put("127.0.0.1", server.port, "/v1/snapshot", body, 2s);
	assert(put.status == 200);

	std::filesystem::remove(persist);
	std::puts("oversized PUT rejected without OOM-ing the process OK");
}

void test_restart_resumes_from_persisted_snapshot() {
	auto persist = temp_path();
	std::string body = R"({"schema_version":1,"version":5,"knobs":{}})";

	{
		detentd::Options opts;
		opts.persist_path = persist;
		RunningServer server(opts);
		auto put = detent_http::http_put("127.0.0.1", server.port, "/v1/snapshot", body, 2s);
		assert(put.status == 200);
	} // server destroyed here

	{
		detentd::Options opts;
		opts.persist_path = persist;
		RunningServer server(opts); // fresh process-equivalent instance, same file
		assert(server.app.current_version() == 5);
		auto get = detent_http::http_get("127.0.0.1", server.port, "/v1/snapshot", 2s);
		assert(get.status == 200);
		assert(get.body == body);
	}

	std::filesystem::remove(persist);
	std::puts("distributor restart resumes from persisted snapshot OK");
}

// A real bug found by concurrency review: try_accept() used to commit the
// in-memory snapshot (current_bytes_/current_version_/has_snapshot_)
// *before* persisting, so a persist failure (rename() throws on a missing/
// unwritable directory) threw with the live state already mutated -- the
// PUT correctly answered 500, but GET/health had already started serving
// the "accepted" update behind it. Confirmed reproducible: a persist_path
// under a directory that doesn't exist.
void test_persist_failure_does_not_publish_the_update() {
	detentd::Options opts;
	opts.persist_path = "/nonexistent-directory-for-detent-test/snapshot.json";
	RunningServer server(opts);

	auto put = detent_http::http_put(
	    "127.0.0.1", server.port, "/v1/snapshot",
	    R"({"schema_version":1,"version":1,"knobs":{}})", 2s);
	assert(put.status == 500);
	assert(server.app.current_version() == 0); // must NOT have advanced

	auto get = detent_http::http_get("127.0.0.1", server.port, "/v1/snapshot", 2s);
	assert(get.status == 404); // must still report "no snapshot published yet"

	std::puts("persist failure does not publish the rejected update OK");
}

void test_patch_upserts_and_deletes_onto_current() {
	RunningServer server({});
	auto put = detent_http::http_put(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":1,"knobs":{
			"a:b":{"type":"bool","default":false},
			"c:d":{"type":"int","default":1}
		}})",
		2s);
	assert(put.status == 200);

	// Patch changes a:b and deletes c:d, leaving nothing else to touch.
	auto patch = detent_http::http_patch(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":2,"knobs":{"a:b":{"type":"bool","default":true},"c:d":null}})",
		2s);
	assert(patch.status == 200);
	assert(patch.body.find("\"accepted_version\":2") != std::string::npos);
	assert(server.app.current_version() == 2);

	auto get = detent_http::http_get("127.0.0.1", server.port, "/v1/snapshot", 2s);
	assert(get.status == 200);
	assert(get.body.find("\"default\":true") != std::string::npos);
	assert(get.body.find("c:d") == std::string::npos); // deleted, not merely unset

	std::puts("PATCH upserts and deletes onto the current snapshot OK");
}

void test_patch_without_a_prior_snapshot_is_rejected() {
	RunningServer server({});
	auto patch = detent_http::http_patch(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":1,"knobs":{"a:b":{"type":"bool","default":true}}})",
		2s);
	assert(patch.status == 400); // nothing to patch onto yet
	assert(server.app.current_version() == 0);
	std::puts("PATCH with no prior snapshot is rejected, not silently seeded OK");
}

void test_patch_stale_version_rejected() {
	RunningServer server({});
	auto put = detent_http::http_put(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":5,"knobs":{"a:b":{"type":"bool","default":false}}})", 2s);
	assert(put.status == 200);

	auto stale_patch = detent_http::http_patch(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":5,"knobs":{"a:b":{"type":"bool","default":true}}})", 2s);
	assert(stale_patch.status == 409);
	assert(server.app.current_version() == 5); // unchanged
	std::puts("PATCH with a stale/duplicate version is rejected OK");
}

// resolve_and_commit's fix is that the resolver (which reads current_snapshot_
// to merge a patch onto) runs WHILE the same unique_lock that then checks
// the version and commits is already held -- there is no window where a
// second writer could interleave a read+merge+commit of its own, unlike a
// "read under a shared lock, merge, then take a unique lock to commit"
// design would have. That's a correctness-by-construction property of a
// single critical section, not something a black-box concurrent-HTTP-
// requests test can reliably probe: this system's own contract requires
// distinct patches to use strictly increasing version numbers (the same
// "two publishers racing the same version number is an accepted, documented
// gap" rule a full PUT already has -- see the README's consistency
// caveat), so racing two real threads at the version numbers non-deterministically
// decides which one is even individually acceptable, independent of any
// merge-base bug -- this exercise's own first attempt at this test hit
// exactly that: `ok_count == 2` failed intermittently under load (the
// higher-version PATCH occasionally committed before the lower-version
// one reached the server, correctly 409-rejecting it per the version gate,
// not because of a lost-update). What IS worth asserting deterministically
// is that two patches to *different* knobs, applied in a known order,
// both survive in the final snapshot -- i.e. apply_patch's merge (not
// resolve_and_commit's locking) actually preserves an unrelated knob across
// more than one accepted patch, not just one.
void test_sequential_patches_to_different_knobs_both_survive() {
	RunningServer server({});
	auto put = detent_http::http_put(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":1,"knobs":{
			"a:one":{"type":"bool","default":false},
			"a:two":{"type":"bool","default":false}
		}})",
		2s);
	assert(put.status == 200);

	auto patch1 = detent_http::http_patch(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":2,"knobs":{"a:one":{"type":"bool","default":true}}})", 2s);
	assert(patch1.status == 200);
	auto patch2 = detent_http::http_patch(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":3,"knobs":{"a:two":{"type":"bool","default":true}}})", 2s);
	assert(patch2.status == 200);
	assert(server.app.current_version() == 3);

	auto get = detent_http::http_get("127.0.0.1", server.port, "/v1/snapshot", 2s);
	assert(get.status == 200);
	// If patch2's merge had discarded anything but the one knob it touched
	// (e.g. by merging onto a fresh/empty Snapshot instead of the current
	// one), a:one's change from patch1 would be missing here.
	assert(get.body.find("\"a:one\":{\"type\":\"bool\",\"default\":true") != std::string::npos);
	assert(get.body.find("\"a:two\":{\"type\":\"bool\",\"default\":true") != std::string::npos);

	std::puts("sequential PATCHes to different knobs both survive OK");
}

void test_multi_step_diff_composes_from_history() {
	RunningServer server({});
	auto put = detent_http::http_put(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":1,"knobs":{
			"a:one":{"type":"bool","default":false},
			"a:two":{"type":"bool","default":false},
			"a:three":{"type":"bool","default":false}
		}})",
		2s);
	assert(put.status == 200);

	auto patch1 = detent_http::http_patch(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":2,"knobs":{"a:one":{"type":"bool","default":true}}})", 2s);
	assert(patch1.status == 200);
	auto patch2 = detent_http::http_patch(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":3,"knobs":{"a:two":{"type":"bool","default":true}}})", 2s);
	assert(patch2.status == 200);
	auto patch3 = detent_http::http_patch(
		"127.0.0.1", server.port, "/v1/snapshot",
		R"({"schema_version":1,"version":4,"knobs":{"a:three":{"type":"bool","default":true}}})", 2s);
	assert(patch3.status == 200);
	assert(server.app.current_version() == 4);

	// A poller whose last-known version is 1 (three updates behind, past
	// the single-step cache) still gets a composed diff, not the full body.
	detent_http::HttpConnection conn("127.0.0.1", server.port, 2s);
	auto resp = conn.get("/v1/snapshot", std::nullopt, /*base_version=*/1);
	assert(resp.status == 200);
	assert(resp.is_patch);
	assert(resp.body.find("\"a:one\":{\"type\":\"bool\",\"default\":true") != std::string::npos);
	assert(resp.body.find("\"a:two\":{\"type\":\"bool\",\"default\":true") != std::string::npos);
	assert(resp.body.find("\"a:three\":{\"type\":\"bool\",\"default\":true") != std::string::npos);

	std::puts("a poller 3 versions behind still gets a composed diff, not the full body OK");
}

} // namespace

int main() {
	test_put_get_conditional_and_rejections();
	test_oversized_put_is_rejected_without_oom();
	test_restart_resumes_from_persisted_snapshot();
	test_persist_failure_does_not_publish_the_update();
	test_patch_upserts_and_deletes_onto_current();
	test_patch_without_a_prior_snapshot_is_rejected();
	test_patch_stale_version_rejected();
	test_sequential_patches_to_different_knobs_both_survive();
	test_multi_step_diff_composes_from_history();
	return 0;
}
