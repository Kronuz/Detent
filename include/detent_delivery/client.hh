#pragma once
#include "detent_transport/tls.hh"
#include "response.hh"
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <chrono>
#include <variant>
namespace detent::delivery {
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
};
// One absolute deadline covers connect, handshake, request and incremental
// reception. Cancellation reaps original network work and budgeted candidate
// disposal before returning. No complete response body is accumulated.
asio::awaitable<ReceivedUpdate> receive(ClientOptions);
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
	InvalidRequest
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
	std::shared_ptr<State> state_;
	friend asio::awaitable<SessionHandle> open_session(ClientOptions, bool);
	friend asio::awaitable<SessionEvent> next_session(ClientOptions, SessionHandle &,
													  std::optional<PublicationFeedback>,
													  std::chrono::milliseconds);
	friend asio::awaitable<void> close_session(ClientOptions, SessionHandle &);
};
asio::awaitable<SessionHandle> open_session(ClientOptions, bool force_snapshot = false);
asio::awaitable<SessionEvent> next_session(ClientOptions, SessionHandle &,
										   std::optional<PublicationFeedback> = {},
										   std::chrono::milliseconds wait = std::chrono::seconds(30));
asio::awaitable<void> close_session(ClientOptions, SessionHandle &);
} // namespace detent::delivery
