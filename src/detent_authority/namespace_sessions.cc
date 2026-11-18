#include "namespace_sessions.hh"
#include <stdexcept>

namespace detent::authority {
NamespaceSessions::NamespaceSessions(std::shared_ptr<distribution::StorageLedger> account, SessionLimits limits)
	: limits_(limits), sessions_(Allocator<std::pair<const Identity, Session>>(account)),
	  principals_(Allocator<std::pair<const Digest, std::size_t>>(account)) {
	if (!limits.sessions || limits.sessions > 65536 || !limits.per_principal ||
		limits.per_principal > limits.sessions || limits.idle_lifetime <= Clock::duration::zero() ||
		limits.maximum_lifetime < limits.idle_lifetime || limits.maximum_lifetime > std::chrono::hours(24))
		throw std::invalid_argument("invalid session limits");
}
bool NamespaceSessions::expired(Session &session, Clock::time_point now) const noexcept {
	if (now >= session.maximum || (!session.busy && now >= session.idle))
		session.closing = true;
	return session.closing;
}
void NamespaceSessions::erase(Sessions::iterator found) noexcept {
	auto principal = principals_.find(found->second.principal.identity);
	if (!--principal->second)
		principals_.erase(principal);
	sessions_.erase(found);
}
SessionResult NamespaceSessions::open(const Identity &id, const SessionPrincipal &principal, bool compression,
									  Clock::time_point now) {
	if (id == Identity{} || principal.identity == Digest{} || !principal.policy_epoch)
		throw std::invalid_argument("invalid session binding");
	if (sessions_.contains(id))
		return SessionResult::Busy;
	auto counted = principals_.find(principal.identity);
	if (sessions_.size() >= limits_.sessions || (counted != principals_.end() && counted->second >= limits_.per_principal))
		return SessionResult::Full;
	if (counted == principals_.end())
		counted = principals_.emplace(principal.identity, 0).first;
	try {
		sessions_.emplace(id, Session{principal, compression, false, false, now + limits_.idle_lifetime,
									  now + limits_.maximum_lifetime});
	} catch (...) {
		if (!counted->second)
			principals_.erase(counted);
		throw;
	}
	++counted->second;
	return SessionResult::Accepted;
}
SessionResult NamespaceSessions::begin(const Identity &id, const SessionPrincipal &principal, Clock::time_point now,
									   bool &compression) {
	auto found = sessions_.find(id);
	if (found == sessions_.end() || expired(found->second, now))
		return SessionResult::Expired;
	auto &session = found->second;
	if (session.principal.identity != principal.identity)
		return SessionResult::Forbidden;
	if (session.principal.policy_epoch != principal.policy_epoch)
		return SessionResult::IdentityChanged;
	if (session.busy)
		return SessionResult::Busy;
	session.busy = true;
	compression = session.compression;
	return SessionResult::Accepted;
}
void NamespaceSessions::end(const Identity &id, Clock::time_point now) noexcept {
	auto found = sessions_.find(id);
	if (found == sessions_.end())
		return;
	auto &session = found->second;
	session.busy = false;
	session.idle = std::min(now + limits_.idle_lifetime, session.maximum);
}
SessionResult NamespaceSessions::close(const Identity &id, const SessionPrincipal &principal) noexcept {
	auto found = sessions_.find(id);
	if (found == sessions_.end())
		return SessionResult::Accepted;
	if (found->second.principal.identity != principal.identity)
		return SessionResult::Forbidden;
	if (found->second.principal.policy_epoch != principal.policy_epoch)
		return SessionResult::IdentityChanged;
	found->second.closing = true;
	if (!found->second.busy)
		erase(found);
	return SessionResult::Accepted;
}
void NamespaceSessions::collect(Clock::time_point now, std::size_t visits) noexcept {
	auto at = sessions_.upper_bound(cursor_);
	for (; visits && !sessions_.empty(); --visits) {
		if (at == sessions_.end())
			at = sessions_.begin();
		cursor_ = at->first;
		auto here = at++;
		if (!here->second.busy && expired(here->second, now))
			erase(here);
	}
}
} // namespace detent::authority
