#include "detent_delivery/client.hh"
#include "session_control_internal.hh"
#include <algorithm>

namespace detent::delivery {
namespace {
void require(bool condition, const char *message) {
	if (!condition)
		throw std::invalid_argument(message);
}
} // namespace
void detail::validate_transport(const ClientOptions &options) {
	require(options.tls_context &&
				(SSL_CTX_get_verify_mode(options.tls_context->native_handle()) & SSL_VERIFY_PEER) &&
				options.endpoint.port() && !options.hostname.empty() && options.hostname.size() <= 253 &&
				options.hostname.find_first_of("\r\n") == std::string::npos &&
				options.hostname.find('\0') == std::string::npos && options.token.size() >= 32 &&
				options.token.size() <= 128 && options.scheduler &&
				std::all_of(options.token.begin(), options.token.end(),
							[](unsigned char c) { return c >= 32 && c != 127; }) &&
				options.timeout > std::chrono::milliseconds::zero() &&
				options.timeout <= std::chrono::hours(24) && options.request_bytes &&
				options.request_bytes <= 1024 * 1024,
			"invalid delivery client options");
}
SessionResetReason detail::session_reset_reason(unsigned status, std::string_view error, std::string_view reason) {
	if (status == 401)
		return SessionResetReason::Unauthorized;
	if (status == 403)
		return SessionResetReason::Forbidden;
	if (status == 410 && error == "session_expired")
		return SessionResetReason::Expired;
	if (status == 409) {
		if (error == "snapshot_required")
			return reason == "representation_limit" ? SessionResetReason::RepresentationLimit
													: SessionResetReason::HistoryLost;
		if (error == "identity_changed")
			return SessionResetReason::IdentityChanged;
		if (error == "session_out_of_sync")
			return SessionResetReason::OutOfSync;
		if (error == "session_busy")
			return SessionResetReason::Busy;
	}
	if (status == 503 && error == "source_not_leader")
		return SessionResetReason::NotLeader;
	if (status == 503 && (error == "outcome_unknown" || error == "stopping" || error == "distribution_unavailable" ||
						  error == "authorization_timeout"))
		return SessionResetReason::Unavailable;
	if (status == 503 && error == "source_behind")
		return SessionResetReason::SourceBehind;
	if (status == 503 || status == 429)
		return SessionResetReason::AdmissionFull;
	if (status == 400)
		return SessionResetReason::InvalidRequest;
	throw std::invalid_argument("unknown session control response");
}
SessionResetError::SessionResetError(SessionResetReason reason)
	: std::runtime_error([&] {
		  switch (reason) {
		  case SessionResetReason::HistoryLost: return "distribution session reset: history_lost";
		  case SessionResetReason::RepresentationLimit: return "distribution session reset: representation_limit";
		  case SessionResetReason::Expired: return "distribution session reset: expired";
		  case SessionResetReason::IdentityChanged: return "distribution session reset: identity_changed";
		  case SessionResetReason::OutOfSync: return "distribution session reset: out_of_sync";
		  case SessionResetReason::Busy: return "distribution session reset: busy";
		  case SessionResetReason::AdmissionFull: return "distribution session reset: admission_full";
		  case SessionResetReason::Forbidden: return "distribution session reset: forbidden";
		  case SessionResetReason::Unauthorized: return "distribution session reset: unauthorized";
		  case SessionResetReason::InvalidRequest: return "distribution session reset: invalid_request";
		  case SessionResetReason::SourceBehind: return "distribution session reset: source_behind";
		  case SessionResetReason::NotLeader: return "distribution session reset: not_leader";
		  case SessionResetReason::Unavailable: return "distribution session reset: unavailable";
		  }
		  return "distribution session reset: unknown";
	  }()), reason_(reason) {}
} // namespace detent::delivery
