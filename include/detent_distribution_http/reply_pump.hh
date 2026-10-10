#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_distribution/protocol.hh"
#include <atomic>
#include <http_handler.h>
#include <optional>

namespace detent::distribution_http {
// Owner-local submission; only authentic transport callbacks change settlement.
class OwnedReplyPump {
  public:
	http::OwnedStreamCapability reply;
	std::atomic<bool> writable{true}, settled{false}, written{false};
	std::shared_ptr<const distribution::OwnedBuffer> control;
	bool started = false;

	static void callbacks(http::StreamOptions &, std::weak_ptr<OwnedReplyPump>);
	void send_control(int status, std::string_view, std::shared_ptr<distribution::StorageLedger>) noexcept;
	void pump_control() noexcept;
	http::StreamSubmit publish(std::shared_ptr<const distribution::OwnedBuffer> &pending);
};
struct SessionHeaders {
	distribution::Identity identity{};
	std::uint64_t window = 0;
};
http::Response update_headers(distribution::UpdateKind, const distribution::Cursor &, bool compressed,
							  std::optional<SessionHeaders> = {});
} // namespace detent::distribution_http
