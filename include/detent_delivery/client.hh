#pragma once
#include "reactor_tls.h"
#include "response.hh"
#include "task_asio.hh"
#include <asio/any_io_executor.hpp>
#include <asio/ip/tcp.hpp>
#include <chrono>
#include <stdexcept>
#include <variant>
namespace detent::delivery {
class ConnectionCache;
struct ConnectionCacheStats {
	std::uint64_t connects = 0, handshakes = 0, reuse_attempts = 0, reuses = 0, retirements = 0, pressure = 0;
	std::size_t live = 0, leased = 0, peak_live = 0;
};
// One owner executor accesses the cache and its counters. Idle sockets count
// toward the caller's FD and TLS memory budgets until the cache retires.
std::shared_ptr<ConnectionCache> make_connection_cache(asio::any_io_executor, std::size_t capacity,
													   std::shared_ptr<distribution::StorageLedger>);
ConnectionCacheStats connection_cache_stats(const std::shared_ptr<ConnectionCache> &);
namespace detail {
struct ProbeAccess;
}
struct ClientOptions {
	asio::ip::tcp::endpoint endpoint;
	std::string hostname, token;
	// Prepared on the startup/configuration worker. Runtime requests never
	// reopen certificate files or relax chain/hostname verification.
	std::shared_ptr<asio::ssl::context> tls_context;
	distribution::Cursor configured;
	std::optional<distribution::Cursor> base;
	std::vector<std::string> namespaces;
	distribution::Limits limits;
	std::chrono::milliseconds timeout{60000};
	std::size_t request_bytes = 256 * 1024;
	bool compression = true;
	std::shared_ptr<ConnectionCache> connections;
	// The loop every operation, timer and socket of a call runs on; required.
	task::asio_scheduler *scheduler = nullptr;
};
// One absolute deadline covers connect, handshake, request and incremental
// reception. Cancellation (the token, or the deadline) reaps original network
// work and budgeted candidate disposal before returning. No complete response
// body is accumulated.
task::task<ReceivedUpdate> receive(ClientOptions, task::cancel_token = {});
struct PublicationFeedback {
	std::uint64_t window;
	distribution::Cursor cursor;
};
enum class SessionResetReason {
	HistoryLost,
	RepresentationLimit,
	Expired,
	IdentityChanged,
	OutOfSync,
	Busy,
	AdmissionFull,
	Forbidden,
	Unauthorized,
	InvalidRequest,
	SourceBehind,
	NotLeader,
	Unavailable
};
class SourceUnavailableError : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
class SourceTrustError : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
class SessionResetError : public std::runtime_error {
  public:
	explicit SessionResetError(SessionResetReason reason);
	SessionResetReason reason() const noexcept { return reason_; }

  private:
	SessionResetReason reason_;
};
struct SessionOffer {
	std::uint64_t window;
	ReceivedUpdate update;
};
struct SessionIdle {};
struct SessionReset {
	SessionResetReason reason;
};
using SessionEvent = std::variant<SessionOffer, SessionIdle, SessionReset>;
// One owner executor operates a handle. Accepted work retains its admitted state.
class SessionHandle {
  public:
	struct State;
	SessionHandle() = default;
	SessionHandle(SessionHandle &&) noexcept = default;
	SessionHandle &operator=(SessionHandle &&) noexcept = default;
	SessionHandle(const SessionHandle &) = delete;
	SessionHandle &operator=(const SessionHandle &) = delete;
	bool valid() const noexcept;
	distribution::Identity identity() const noexcept;
	std::chrono::milliseconds maximum_wait() const noexcept;

  private:
	friend struct detail::ProbeAccess;
	std::shared_ptr<State> state_;
	friend task::task<SessionHandle> open_session(ClientOptions, bool, task::cancel_token);
	friend task::task<SessionEvent> next_session(ClientOptions, SessionHandle &, std::optional<PublicationFeedback>,
												 std::chrono::milliseconds, task::cancel_token);
	friend task::task<void> close_session(ClientOptions, SessionHandle &, task::cancel_token);
};
task::task<SessionHandle> open_session(ClientOptions, bool force_snapshot = false, task::cancel_token = {});
task::task<SessionEvent> next_session(ClientOptions, SessionHandle &, std::optional<PublicationFeedback> = {},
									  std::chrono::milliseconds wait = std::chrono::seconds(30),
									  task::cancel_token = {});
// Invalidates Next immediately. A failed Close may be explicitly retried;
// authenticated 204/410 settles it and makes subsequent Close calls local no-ops.
task::task<void> close_session(ClientOptions, SessionHandle &, task::cancel_token = {});
} // namespace detent::delivery
