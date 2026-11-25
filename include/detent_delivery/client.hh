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
struct ClientOptions {
	asio::ip::tcp::endpoint endpoint;
	std::string hostname, token;
	// Prepared on the startup/configuration worker. Runtime requests never
	// reopen certificate files or relax chain/hostname verification.
	std::shared_ptr<asio::ssl::context> tls_context;
	distribution::Limits limits;
	std::chrono::milliseconds timeout{60000};
	std::size_t request_bytes = 256 * 1024;
	bool compression = true;
	std::shared_ptr<ConnectionCache> connections;
	// The loop every operation, timer and socket of a call runs on; required.
	task::asio_scheduler *scheduler = nullptr;
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
} // namespace detent::delivery
