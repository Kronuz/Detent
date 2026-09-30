#pragma once

// RaftDelegate<RaftNode> for cluster::Raft, replacing detentd's old best-
// effort gossip with real leader election + replicated writes. Design
// rationale, the prefers()/restart-safety subtleties: see CLUSTERING.md.

#include "detent/errors.hh"
#include "detent/result.hh"

#include <raft.h>

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace cluster { class Bus; }

namespace detentd {

class Distributor;

// A cluster member's identity: its own advertised "host:port" HTTP endpoint
// (same string as Options::Cluster::peers).
struct RaftNode {
	std::string address; // "" == empty/no node (cluster::Raft's sentinel)
	[[nodiscard]] bool operator==(const RaftNode&) const = default;
};

class DetentRaftDelegate : public cluster::RaftDelegate<RaftNode> {
public:
	// RaftDelegate<RaftNode>'s template parameter isn't a nested type alias
	// on the base, so plain `Node` below needs this to resolve at all.
	using Node = RaftNode;

	// self_address is this node's identity; peers is every other member's
	// address. startup_silence is how long, from construction, active()
	// reports false (see CLUSTERING.md's restart-safety section).
	// liveness_timeout bounds is_alive()/alive_nodes() (see their comments).
	DetentRaftDelegate(Distributor& owner, cluster::Bus& bus, std::string self_address,
	                    std::vector<std::string> peers, std::chrono::steady_clock::duration startup_silence,
	                    std::chrono::steady_clock::duration liveness_timeout);

	void broadcast(cluster::RaftMessage msg, const std::string& payload) override;
	Node local_node() override { return RaftNode{self_address_}; }
	std::string serialise(const Node& node) override;
	std::optional<Node> parse_node(const char** p, const char* end) override;
	std::string node_id(const Node& node) override { return node.address; }

	std::size_t total_nodes() override { return known_nodes_.size(); }
	// Counts nodes is_alive() currently accepts (including self).
	std::size_t alive_nodes() override;
	// True until a peer has been silent for longer than liveness_timeout_
	// after having been heard from at least once (benefit of the doubt
	// before first contact). Drives which peer's next_index the leader's
	// heartbeat_cb() picks to broadcast (raft.h) -- a permanently-dead
	// node reported as alive forever freezes that pick at its last known
	// (stale) next_index, blocking every later entry from ever committing.
	bool is_alive(const std::string& id) override;

	// Identity, not unconditional false -- see CLUSTERING.md ("prefers():
	// not a no-op hook") for why the reflexive case matters here.
	bool prefers(const Node& a, const Node& b) override { return a.address == b.address; }

	// No cluster-bootstrap lifecycle; active() instead gates a restart-
	// safety window (see CLUSTERING.md).
	bool active() override { return std::chrono::steady_clock::now() >= active_since_; }
	bool ready() override { return true; }
	bool joining() override { return false; }
	void ensure_setup() override {}

	void set_leader(const Node& leader) override;
	void apply(const std::string& command) override;

	[[nodiscard]] bool quorum(std::size_t total, std::size_t count) override { return count * 2 > total; }

private:
	Distributor& owner_;
	cluster::Bus& bus_;
	std::string self_address_;
	std::set<std::string> known_nodes_;
	std::chrono::steady_clock::time_point active_since_;
	std::chrono::steady_clock::duration liveness_timeout_;
	std::map<std::string, std::chrono::steady_clock::time_point> last_seen_;
};

// Maps a Bus datagram's wire `type` to its logical RaftMessage, or nullopt
// if outside Raft's range.
[[nodiscard]] std::optional<cluster::RaftMessage> raft_message_for_wire_type(int wire_type);

// One past the highest valid Raft wire type; pass as cluster::Bus's max_type.
inline constexpr int kMaxRaftWireType = 1 + static_cast<int>(cluster::RaftMessage::ADD_COMMAND) + 1;

} // namespace detentd
