#pragma once
#include "detent_relay/namespace_relay.hh"
#include <asio/ip/tcp.hpp>
#include <asio/ssl/context.hpp>
#include <chrono>
#include <memory>

// An Upstream over HTTPS (W8; docs/status/namespace-s8.md, "The service"): the authority's voters or
// an upstream relay, which answer the same routes, `POST /v2/history/layouts`, `/v2/history/file`
// and W5's `/v2/distribution/session/{open,next}`. A relay reads them as any reader does, with a
// credential and no consensus. Blocking, one request per connection, one caller at a time.
namespace detent::relay {
struct HttpUpstreamOptions {
	std::vector<asio::ip::tcp::endpoint> endpoints; // read in turn: a failure, a lagging answer or a 503 moves on
	std::string hostname, token;
	std::shared_ptr<asio::ssl::context> tls; // a client context that verifies the upstream
	std::chrono::milliseconds timeout{10000};
	std::size_t maximum_bytes = std::size_t(256) << 20; // one response body
	// One history request's length (D138): a larger file is read in ranges of it. Under the
	// authority's default reply bound (16 MiB) with room for a second reply in flight.
	std::size_t range_bytes = std::size_t(4) << 20;
	bool compression = false;                            // W5 answers in zstd
};
// The upstream did not answer: unreachable, timed out, or a status other than the route's.
class UpstreamUnavailable : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
class HttpUpstream final : public Upstream {
  public:
	explicit HttpUpstream(HttpUpstreamOptions);
	~HttpUpstream() override;
	MirrorTable layouts(std::span<const std::string> names) override;
	// A file of at most `range_bytes` in one request; a larger one in ranges (D138), each asked of
	// the next source again when its answer is lost, every source once. The verified prefix outlives
	// a fetch that fails, and the next fetch of the same file resumes at its end. The whole file is
	// checked against the layout's size and digest; a mismatch drops the prefix.
	std::string fetch(std::string_view name_space, const MirrorFile &) override;
	// One W5 session per endpoint, opened on first use and again when it expires; every next
	// carries its positions with `wait_ms` 0. An idle answer (204) is a body with no frames. A
	// session still retiring its previous answer (409 `session_busy`) is asked again after a wait.
	std::string frames(std::span<const Position>) override;
	std::size_t sources() const noexcept override;
	void skip() override;
	struct Stats {
		std::uint64_t requests = 0, failures = 0, rotations = 0, sessions = 0;
		std::uint64_t fetched_files = 0, fetched_bytes = 0;
		std::uint64_t ranges = 0, resumed = 0; // range requests answered; fetches resumed from a prefix
	};
	Stats stats() const noexcept;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::relay
