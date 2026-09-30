#include "raft_delegate.hh"
#include "detentd.hh"

#include <bus.h>
#include <length.h>

namespace detentd {

namespace {

// RaftMessage's 7 logical values (REQUEST_VOTE=0 .. ADD_COMMAND=6) map onto
// Bus wire types 1..7. See CLUSTERING.md for the full rationale.
constexpr int kRaftWireTypeBase = 1;

int wire_type_for(cluster::RaftMessage msg) { return kRaftWireTypeBase + static_cast<int>(msg); }

std::optional<cluster::RaftMessage> raft_message_for(int wire_type) {
	auto offset = wire_type - kRaftWireTypeBase;
	if (offset < 0 || offset > static_cast<int>(cluster::RaftMessage::ADD_COMMAND)) return std::nullopt;
	return static_cast<cluster::RaftMessage>(offset);
}

} // namespace

// Exposed for Distributor's Bus on_message dispatch.
std::optional<cluster::RaftMessage> raft_message_for_wire_type(int wire_type) { return raft_message_for(wire_type); }

DetentRaftDelegate::DetentRaftDelegate(
	Distributor& owner, cluster::Bus& bus, std::string self_address, std::vector<std::string> peers,
	std::chrono::steady_clock::duration startup_silence, std::chrono::steady_clock::duration liveness_timeout)
	: owner_(owner), bus_(bus), self_address_(std::move(self_address)),
	  active_since_(std::chrono::steady_clock::now() + startup_silence), liveness_timeout_(liveness_timeout) {
	known_nodes_.insert(self_address_);
	for (auto& p : peers) known_nodes_.insert(std::move(p));
}

void DetentRaftDelegate::broadcast(cluster::RaftMessage msg, const std::string& payload) {
	bus_.send(wire_type_for(msg), payload);
}

std::string DetentRaftDelegate::serialise(const Node& node) { return cluster::serialise_string(node.address); }

std::optional<DetentRaftDelegate::Node> DetentRaftDelegate::parse_node(const char** p, const char* end) {
	std::string_view sv;
	if (!cluster::unserialise_string(p, end, sv)) return std::nullopt;
	std::string address(sv);
	// Not a configured member (stale/malicious datagram sharing our token).
	if (address.empty() || known_nodes_.count(address) == 0) return std::nullopt;
	last_seen_[address] = std::chrono::steady_clock::now(); // this IS a liveness signal, any Raft message
	return RaftNode{std::move(address)};
}

bool DetentRaftDelegate::is_alive(const std::string& id) {
	if (id == self_address_) return true;
	auto it = last_seen_.find(id);
	if (it == last_seen_.end()) return true; // never contacted yet; benefit of the doubt
	return std::chrono::steady_clock::now() - it->second < liveness_timeout_;
}

std::size_t DetentRaftDelegate::alive_nodes() {
	std::size_t n = 0;
	for (const auto& id : known_nodes_) n += is_alive(id) ? 1 : 0;
	return n;
}

void DetentRaftDelegate::set_leader(const Node& leader) { owner_.on_raft_leader_changed(leader.address); }

void DetentRaftDelegate::apply(const std::string& command) { owner_.on_raft_apply(command); }

} // namespace detentd
