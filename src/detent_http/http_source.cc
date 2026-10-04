#include "detent_http/http_source.hh"
#include "detent_http/http_client.hh"

#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <random.hh>

namespace detent_http {

namespace {

// One reused keep-alive connection per source (Section 3: exactly one
// KnobSource stream per client instance -> exactly one connection). The
// closure captures a shared_ptr so std::function stays copyable.
struct PollState {
	HttpConnection connection;
	std::optional<std::string> last_etag;

	PollState(
		std::string host, std::uint16_t port, std::chrono::milliseconds timeout,
		std::chrono::milliseconds max_connection_age)
		: connection(std::move(host), port, timeout, max_connection_age) {}
};

// unique_ptr per connection (not a plain vector<HttpConnection>): the class
// deletes its copy ctor and declares its own destructor, so it has no
// implicit move either -- a Connection can't live directly in a vector that
// might need to relocate its elements.
struct MultiPollState {
	std::vector<std::unique_ptr<HttpConnection>> connections;
	std::size_t current = 0;
	std::optional<std::string> last_etag;

	MultiPollState(
		std::vector<std::pair<std::string, std::uint16_t>> nodes, std::chrono::milliseconds timeout,
		std::chrono::milliseconds max_connection_age) {
		connections.reserve(nodes.size());
		for (auto& node : nodes) {
			connections.push_back(
				std::make_unique<HttpConnection>(std::move(node.first), node.second, timeout, max_connection_age));
		}
		// A random starting node, not always index 0, spreads a fleet of
		// independent client processes (each constructed with the same
		// node list) across the cluster without per-request randomization,
		// which would defeat the whole point of a reused keep-alive
		// connection.
		current = static_cast<std::size_t>(random_int(0, connections.size() - 1));
	}
};

} // namespace

detent::InitOptions::SourcePoll make_http_poll_source(
	std::string host, std::uint16_t port, std::string path, std::chrono::milliseconds request_timeout,
	std::chrono::milliseconds max_connection_age) {
	auto state = std::make_shared<PollState>(std::move(host), port, request_timeout, max_connection_age);

	// after_version (Client's own last-accepted version) is sent as
	// X-Detent-Base-Version: a hint, not a request the server is obligated
	// to honor -- detentd answers with its cached diff only if that's
	// EXACTLY the version its diff was computed against, and falls back to
	// the full body otherwise (see detentd.hh's current_diff_bytes_
	// comment). This is a pure bandwidth optimization layered onto the
	// existing ETag-conditional GET, not a replacement for it: the 304 path
	// below is unaffected either way.
	return [path = std::move(path), state](std::uint64_t after_version) -> std::optional<detent::InitOptions::SourcePayload> {
		auto resp = state->connection.get(path, state->last_etag, after_version);
		if (resp.status == 304) {
			return std::nullopt; // unchanged since last_etag; not an error, no backoff
		}
		if (resp.status == 404) {
			return std::nullopt; // distributor has no snapshot published yet
		}
		if (resp.status != 200) {
			throw std::runtime_error("detent_http: unexpected status " + std::to_string(resp.status) +
			                          " from " + path);
		}
		if (resp.etag) {
			state->last_etag = resp.etag;
		}
		return detent::InitOptions::SourcePayload{std::move(resp.body), resp.is_patch};
	};
}

detent::InitOptions::SourcePoll make_http_poll_source(
	std::vector<std::pair<std::string, std::uint16_t>> nodes, std::string path,
	std::chrono::milliseconds request_timeout, std::chrono::milliseconds max_connection_age) {
	if (nodes.empty()) {
		throw std::invalid_argument("detent_http: make_http_poll_source needs at least one node");
	}
	auto state = std::make_shared<MultiPollState>(std::move(nodes), request_timeout, max_connection_age);

	return [path = std::move(path), state](std::uint64_t after_version) -> std::optional<detent::InitOptions::SourcePayload> {
		auto n = state->connections.size();
		std::exception_ptr last_error;
		for (std::size_t attempt = 0; attempt < n; ++attempt) {
			try {
				auto resp = state->connections[state->current]->get(path, state->last_etag, after_version);
				if (resp.status == 304) return std::nullopt;
				if (resp.status == 404) return std::nullopt;
				if (resp.status != 200) {
					throw std::runtime_error("detent_http: unexpected status " + std::to_string(resp.status) +
					                          " from " + path);
				}
				if (resp.etag) state->last_etag = resp.etag;
				return detent::InitOptions::SourcePayload{std::move(resp.body), resp.is_patch};
			} catch (const std::exception&) {
				// Transport failure against the current node (not a bad
				// status, which is thrown above and caught here too but
				// means the SERVER answered, just not usefully): sticky
				// failover to the next node, same round-robin reasoning
				// as the constructor's random start -- see the header.
				last_error = std::current_exception();
				state->current = (state->current + 1) % n;
			}
		}
		std::rethrow_exception(last_error); // every node failed this round
	};
}

} // namespace detent_http
