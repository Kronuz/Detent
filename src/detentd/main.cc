// The distributor executable: wires Distributor into http::HttpAsioService
// and runs until stopped.
//
// Usage:
//   detentd <port> <persist_path>
//       [--reactors N] [--workers N]
//       [--cluster <token> <multicast_port> <multicast_group> <interface>
//                  <self_host:port> <peer_host:port> [<peer_host:port> ...]]
//
// Example (three same-host replicas forming a Raft cluster over loopback
// multicast, each told its OWN advertised HTTP endpoint plus every peer's):
//   detentd 8081 /tmp/jk-a.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083
//   detentd 8082 /tmp/jk-b.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8082 127.0.0.1:8081 127.0.0.1:8083
//   detentd 8083 /tmp/jk-c.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8083 127.0.0.1:8081 127.0.0.1:8082
#include "detentd.hh"

#include <http_asio.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {
std::atomic<bool> g_stop{false};
void handle_signal(int) { g_stop.store(true); }

const char* kUsage =
	"usage: detentd <port> <persist_path> [--reactors N] [--workers N]\n"
	"                [--cluster <token> <multicast_port> <multicast_group>\n"
	"                           <interface> <self_host:port> <peer_host:port> ...]\n"
	"                [--raft-timing <heartbeat_timeout> <election_init>\n"
	"                               <election_min> <election_max>]\n"
	"\n"
	"detentd: the HTTP distribution-layer daemon -- serves GET/PUT/PATCH\n"
	"/v1/snapshot to clients, persists to <persist_path>, and optionally\n"
	"forms a Raft cluster with peer detentd instances over UDP multicast:\n"
	"only the elected leader accepts writes (a follower answers with the\n"
	"leader's address instead), and a write isn't reported accepted until a\n"
	"quorum of the cluster has it.\n"
	"\n"
	"--raft-timing overrides cluster::Raft's default timing (1.0/4.0/10.0/\n"
	"30.0 seconds) -- e.g. for a fast end-to-end test harness; every node\n"
	"in one cluster should use the same values.\n"
	"\n"
	"example (three same-host replicas, each told its OWN advertised\n"
	"endpoint plus every peer's):\n"
	"  detentd 8081 /tmp/jk-a.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083\n"
	"  detentd 8082 /tmp/jk-b.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8082 127.0.0.1:8081 127.0.0.1:8083\n"
	"  detentd 8083 /tmp/jk-c.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8083 127.0.0.1:8081 127.0.0.1:8082\n";
} // namespace

int main(int argc, char** argv) {
	if (argc >= 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
		std::fputs(kUsage, stdout);
		return 0;
	}
	if (argc < 3) {
		std::fprintf(stderr, "%s", kUsage);
		return 2;
	}

	auto port = static_cast<unsigned short>(std::atoi(argv[1]));
	detentd::Options options;
	options.persist_path = argv[2];

	std::size_t reactors = 4;
	std::size_t workers = 8;
	cluster::RaftConfig raft_timing; // defaults; --raft-timing overrides below

	for (int i = 3; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--reactors" && i + 1 < argc) {
			reactors = static_cast<std::size_t>(std::atoi(argv[++i]));
		} else if (arg == "--workers" && i + 1 < argc) {
			workers = static_cast<std::size_t>(std::atoi(argv[++i]));
		} else if (arg == "--raft-timing" && i + 4 < argc) {
			raft_timing.heartbeat_timeout = std::atof(argv[++i]);
			raft_timing.election_init = std::atof(argv[++i]);
			raft_timing.election_min = std::atof(argv[++i]);
			raft_timing.election_max = std::atof(argv[++i]);
		} else if (arg == "--cluster" && i + 5 < argc) {
			detentd::Options::Cluster cluster;
			cluster.token = argv[++i];
			cluster.multicast_port = static_cast<unsigned short>(std::atoi(argv[++i]));
			cluster.multicast_group = argv[++i];
			cluster.multicast_interface = argv[++i];
			cluster.self_address = argv[++i];
			while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
				cluster.peers.emplace_back(argv[++i]);
			}
			options.cluster = std::move(cluster);
		}
	}
	if (options.cluster) options.cluster->raft_config = raft_timing;

	detentd::Distributor app(options);
	http::HttpAsioService service(app, reactors, workers, /*queue_limit=*/1024);
	service.enable_conditional(); // GET /v1/snapshot's automatic ETag / 304 short-circuit

	std::signal(SIGINT, handle_signal);
	std::signal(SIGTERM, handle_signal);
	// Belt-and-suspenders alongside the MSG_NOSIGNAL/SO_NOSIGPIPE guards on
	// detent_http's own send() calls (src/detent_http/http_client.cc): any
	// write to a socket whose peer has already reset the connection -- ours
	// or a future one in a dependency we don't control -- must not be able
	// to take the whole distributor down via the default SIGPIPE
	// disposition (process termination). EPIPE from write()'s return value
	// is the correct, already-handled failure path.
	std::signal(SIGPIPE, SIG_IGN);

	service.start(port);
	std::printf("detentd listening on :%u, persisting to %s\n", port, argv[2]);

	while (!g_stop.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds{200});
	}

	std::printf("shutting down...\n");
	service.stop();
	return 0;
}
