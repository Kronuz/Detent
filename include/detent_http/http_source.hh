#pragma once

// The client-side half of the HTTP distribution path: an
// InitOptions::SourcePoll implementation that polls a detent distributor
// (see distributor/) via conditional GET. This is the "efficient at scale"
// choice over a blocking long-poll: the distributor's handler for GET
// /v1/snapshot never blocks (Kronuz/http's handlers run synchronously to
// completion -- there is no deferred-completion path in the current
// framework, confirmed by reading detail::serve_connection, not assumed), so
// a fleet of pollers costs the server one cheap conditional-GET round trip
// per client per interval rather than one pinned worker thread per waiting
// connection. Freshness trades instant propagation for poll_interval
// latency, squarely inside the spec's own "eventual, not transactional"
// contract (Section 3).

#include "detent/client.hh"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace detent_http {

// `path` defaults to the distributor's always-instant, ETag-conditional
// endpoint (GET /v1/snapshot), not a long-poll endpoint -- there isn't one.
// `request_timeout` bounds a single HTTP round trip; Client's own
// poll_interval (InitOptions::poll_interval) controls how often this is
// called at all. `max_connection_age`, if nonzero, forces a periodic
// reconnect (fresh DNS resolution) even on an otherwise-healthy connection
// -- see HttpConnection's own comment (http_client.hh) for why: `host`
// behind round-robin/health-checked DNS (instead of an explicit node list)
// only ever gets load-balanced at the moment of (re)connection, and this
// client deliberately keeps ONE connection alive indefinitely otherwise.
[[nodiscard]] detent::InitOptions::SourcePoll make_http_poll_source(
	std::string host,
	std::uint16_t port,
	std::string path = "/v1/snapshot",
	std::chrono::milliseconds request_timeout = std::chrono::seconds{5},
	std::chrono::milliseconds max_connection_age = {});

// Multi-node overload, for polling a Raft cluster (see CLUSTERING.md)
// instead of one fixed distributor: `nodes` names every peer this poller
// may read from. Reads don't need a leader -- every node applies the
// identical committed sequence in the identical order (same CLUSTERING.md
// section), so an ETag/diff from one node is valid against another. One
// node is picked at random at construction (so a fleet of independent
// client processes given the same list spreads across the cluster instead
// of piling onto node 0) and stays sticky -- preserving the one-reused-
// keep-alive-connection design that avoids per-poll TCP handshake churn --
// until a request to it fails, at which point the next node in the list is
// tried, up to `nodes.size()` attempts before this poll call gives up.
// `max_connection_age`: see the single-host overload above; applies to
// whichever node this poller is currently stuck to.
[[nodiscard]] detent::InitOptions::SourcePoll make_http_poll_source(
	std::vector<std::pair<std::string, std::uint16_t>> nodes,
	std::string path = "/v1/snapshot",
	std::chrono::milliseconds request_timeout = std::chrono::seconds{5},
	std::chrono::milliseconds max_connection_age = {});

} // namespace detent_http
