#pragma once
// W5 sessions on the owner (docs/status/namespace-s6-cutover.md): a session holds its principal and
// coding, at most one next in flight, and expires when idle or at its maximum lifetime. Every next
// carries its own cursors, so a session holds no position.
#include "detent_distribution/allocator.hh"
#include "state.hh"
#include <chrono>
#include <map>

namespace detent::authority {
struct SessionPrincipal {
	Digest identity{};
	std::uint64_t policy_epoch = 0;
	bool operator==(const SessionPrincipal &) const = default;
};
struct SessionLimits {
	std::size_t sessions = 1024, per_principal = 256;
	std::chrono::milliseconds idle_lifetime{60000}, maximum_lifetime{600000};
};
enum class SessionResult { Accepted, Busy, Expired, Forbidden, IdentityChanged, Full };
class NamespaceSessions {
  public:
	using Clock = std::chrono::steady_clock;
	NamespaceSessions(std::shared_ptr<distribution::StorageLedger>, SessionLimits);
	SessionResult open(const Identity &id, const SessionPrincipal &, bool compression, Clock::time_point now);
	// A next begins: Accepted holds the session until end(); `compression` is the session's coding.
	SessionResult begin(const Identity &id, const SessionPrincipal &, Clock::time_point now, bool &compression);
	void end(const Identity &id, Clock::time_point now) noexcept;
	SessionResult close(const Identity &id, const SessionPrincipal &) noexcept;
	// Removes expired sessions with no next in flight, visiting at most `visits`.
	void collect(Clock::time_point now, std::size_t visits) noexcept;
	std::size_t size() const noexcept { return sessions_.size(); }

  private:
	struct Session {
		SessionPrincipal principal;
		bool compression = false, busy = false, closing = false;
		Clock::time_point idle, maximum;
	};
	template <class T> using Allocator = distribution::StorageAllocator<T>;
	using Sessions = std::map<Identity, Session, std::less<>, Allocator<std::pair<const Identity, Session>>>;
	using Principals =
		std::map<Digest, std::size_t, std::less<>, Allocator<std::pair<const Digest, std::size_t>>>;
	bool expired(Session &, Clock::time_point now) const noexcept;
	void erase(Sessions::iterator) noexcept;
	SessionLimits limits_;
	Sessions sessions_;
	Principals principals_;
	Identity cursor_{};
};
} // namespace detent::authority
