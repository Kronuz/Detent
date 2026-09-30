#pragma once

// A minimal, dependency-light blocking HTTP/1.1 client: plain POSIX sockets,
// no Asio/reactor/http-parser. Deliberately NOT built on the Kronuz/http
// stack -- that stack is the *server*-side event-driven machinery (see
// distributor/); a client issuing requests one at a time needs none of it,
// and pulling it in here would force every consumer of detent_http to
// also pull Asio.
//
// Two shapes, on purpose:
//   - http_get/http_put: one-shot, "Connection: close", read-to-EOF. Simple,
//     and fine for infrequent calls (the publisher CLI, a gossip-triggered
//     peer pull, ad hoc tests).
//   - HttpConnection: a REUSED keep-alive connection with real
//     Content-Length-aware parsing. This is what make_http_poll_source()
//     uses for its repeated polling, and it matters: an earlier version of
//     this client always closed after one request, and a load test at 1,000
//     concurrent pollers showed p99 fan-out latency balloon to ~5.5s versus
//     ~100ms at 50 pollers -- almost certainly the cost of a fresh TCP
//     handshake/teardown on every single poll, not the distributor's actual
//     request handling. Reusing one connection per poller removes that
//     churn; see distributor/tests/test_load_fanout.cc for the re-measured
//     numbers.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace detent_http {

struct HttpResponse {
	int status = 0;
	std::string body;
	std::optional<std::string> etag;
	// True when the response's Content-Type is application/merge-patch+json
	// -- detentd's diff-since-last-version response (see detentd.hh's
	// current_diff_bytes_ comment) instead of a full snapshot. Callers
	// (make_http_poll_source) surface this straight through to
	// detent::InitOptions::SourcePayload::is_patch; detent::Client is the
	// one place that actually parses/applies either shape.
	bool is_patch = false;
};

// Throws std::runtime_error on DNS/connect/timeout/transport failure. A
// well-formed HTTP response (any status code) is returned normally, never
// thrown -- only transport-level failure is exceptional.
[[nodiscard]] HttpResponse http_get(
	const std::string& host,
	std::uint16_t port,
	const std::string& path,
	std::chrono::milliseconds timeout,
	const std::optional<std::string>& if_none_match = std::nullopt);

// Same transport, for the distributor's own PUT /v1/snapshot publisher path
// and for peer-to-peer gossip pulls.
[[nodiscard]] HttpResponse http_put(
	const std::string& host,
	std::uint16_t port,
	const std::string& path,
	std::string body,
	std::chrono::milliseconds timeout);

// Same transport, PATCH instead of PUT: for a publisher that wants to send
// only the one or few knobs that changed (see json_patch.hh) instead of a
// full snapshot -- detentd's PATCH /v1/snapshot ingest.
[[nodiscard]] HttpResponse http_patch(
	const std::string& host,
	std::uint16_t port,
	const std::string& path,
	std::string body,
	std::chrono::milliseconds timeout);

// A single reused HTTP/1.1 keep-alive connection to one host:port. Not
// thread-safe -- one instance per polling thread/Client, matching how
// make_http_poll_source uses it (Section 3: exactly one KnobSource stream
// per client instance, so exactly one connection). Reconnects transparently
// once on a stale/broken connection (e.g. the server's own idle-connection
// timeout) before giving up and throwing.
class HttpConnection {
public:
	// `max_connection_age`, if nonzero, forces a proactive reconnect (fresh
	// DNS resolution) once the current connection has lived at least that
	// long, even though it's still healthy -- otherwise a long-lived
	// keep-alive connection resolved `host` exactly once, at first connect,
	// and never re-resolves again unless the connection actually breaks. A
	// single DNS name load-balanced via round-robin/health-checked A
	// records (as opposed to an explicit per-node list) needs this to ever
	// move off whichever address it first happened to land on. Zero
	// (default) disables this and matches the prior, unconditional-reuse
	// behavior exactly.
	HttpConnection(
		std::string host, std::uint16_t port, std::chrono::milliseconds timeout,
		std::chrono::milliseconds max_connection_age = {});
	~HttpConnection();

	HttpConnection(const HttpConnection&) = delete;
	HttpConnection& operator=(const HttpConnection&) = delete;

	// `base_version`, if given, is sent as X-Detent-Base-Version: a hint to
	// a detentd server that this caller's own last-accepted version is
	// exactly that one, so a body-changed response MAY come back as a
	// small diff (HttpResponse::is_patch) instead of a full snapshot --
	// see detentd.hh's current_diff_bytes_ comment. Purely a bandwidth
	// optimization: omitting it (or a version the server doesn't have a
	// cached diff for) just gets the full body, same as before this existed.
	[[nodiscard]] HttpResponse get(
		const std::string& path,
		const std::optional<std::string>& if_none_match = std::nullopt,
		std::optional<std::uint64_t> base_version = std::nullopt);

private:
	[[nodiscard]] HttpResponse get_once(
		const std::string& path, const std::optional<std::string>& if_none_match,
		std::optional<std::uint64_t> base_version);
	void ensure_connected();
	void disconnect() noexcept;

	std::string host_;
	std::uint16_t port_;
	std::chrono::milliseconds timeout_;
	std::chrono::milliseconds max_connection_age_;
	std::chrono::steady_clock::time_point connected_at_{};
	int fd_ = -1;
};

} // namespace detent_http
