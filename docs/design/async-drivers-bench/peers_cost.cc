// Review F6: what today's per-group transport costs an idle member. One consensus::Peers per
// group (transport/peers.h:53-91): its own TLS context, a bound listener, and a Control and a
// Bulk connection per peer, each with a suspended reader and writer. Builds an n-voter group of
// idle members (Worker, SnapshotChannel, Peers) in one process on one io_context over loopback
// mutual TLS, waits until every member holds 2(n-1) ready connections, and prints the
// per-member heap (operator new and OpenSSL's allocator, usable sizes), descriptors and
// threads the Peers added.   peers_cost <certificate dir> [voters...]
#include "probe.hh"
PROBE_ALLOCATOR

#include "completion/posix.h"
#include "consensus/transport/peers.h"
#include "consensus/transport/tls.h"

#include <asio/io_context.hpp>
#include <openssl/crypto.h>
#include <openssl/pem.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace j = kronuz::journal;
namespace c = kronuz::consensus;
namespace t = kronuz::consensus::transport;

namespace {
// OpenSSL allocates with its own functions; count them like operator new.
void* ssl_malloc(std::size_t n, const char*, int) {
	void* p = std::malloc(n ? n : 1);
	if (p) {
		probe::live_bytes.fetch_add(static_cast<std::int64_t>(usable(p)), std::memory_order_relaxed);
		probe::live_blocks.fetch_add(1, std::memory_order_relaxed);
	}
	return p;
}
void ssl_free(void* p, const char*, int) {
	if (!p) { return; }
	probe::live_bytes.fetch_sub(static_cast<std::int64_t>(usable(p)), std::memory_order_relaxed);
	probe::live_blocks.fetch_sub(1, std::memory_order_relaxed);
	std::free(p);
}
void* ssl_realloc(void* p, std::size_t n, const char* file, int line) {
	if (!p) { return ssl_malloc(n, file, line); }
	if (!n) {
		ssl_free(p, file, line);
		return nullptr;
	}
	const auto old = static_cast<std::int64_t>(usable(p));
	void* q = std::realloc(p, n);
	if (q) { probe::live_bytes.fetch_add(static_cast<std::int64_t>(usable(q)) - old, std::memory_order_relaxed); }
	return q;
}

const j::AdmissionLimits admission{{1024ull * 1024 * 1024, 65536}, {8ull * 1024 * 1024, 3}, {512ull * 1024 * 1024, 4}, 64, 3};

struct Member {
	std::unique_ptr<c::Worker> worker_;
	std::unique_ptr<c::SnapshotChannel> channel_;
	c::Worker& worker() { return *worker_; }
	c::SnapshotChannel& channel() { return *channel_; }
};

t::Fingerprint pin(const fs::path& path) {
	std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_file(path.c_str(), "r"), BIO_free);
	if (!input) { throw std::runtime_error("certificate " + path.string()); }
	std::unique_ptr<X509, decltype(&X509_free)> certificate(PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
	return t::certificate_fingerprint(certificate.get());
}

void group(const fs::path& certificates, const fs::path& root, unsigned voters, bool print = true) {
	asio::io_context io;
	std::vector<c::NodeId> ids;
	for (unsigned i = 1; i <= voters; ++i) { ids.push_back(i); }
	c::FixedConfiguration base;
	base.cluster[0] = 'B';
	base.configuration[0] = 'P';
	base.configuration[15] = 1;
	base.voters = ids;

	// Idle members first: a Worker on its own journal, created and started, and its channel.
	std::vector<std::shared_ptr<Member>> members;
	for (unsigned i = 1; i <= voters; ++i) {
		static int run = 0;
		auto path = root / ("r" + std::to_string(++run) + "-v" + std::to_string(voters) + "-n" + std::to_string(i));
		fs::create_directories(path);
		fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
		auto config = base;
		config.local = i;
		auto member = std::make_shared<Member>();
		member->worker_ = std::make_unique<c::Worker>(std::make_shared<j::PosixIO>(path), config, admission, c::Limits{}, c::Timing{},
			c::WorkerLimits{}, c::SnapshotLimits{0x44544101, 64ull << 20});
		j::Identity identity{};
		identity[0] = static_cast<char>(i);
		member->worker_->create(identity);
		for (int k = 0; k < 64 && !member->worker_->ready(); ++k) {
			member->worker_->run_one({1, 1500});
			while (auto operation = member->worker_->take_io_operation()) { j::drive_synchronously(*operation); }
			member->worker_->take_actions();
		}
		if (!member->worker_->ready()) { throw std::runtime_error("member not ready"); }
		member->channel_ = std::make_unique<c::SnapshotChannel>(*member->worker_);
		members.push_back(std::move(member));
	}
	std::vector<asio::ip::tcp::endpoint> endpoints;
	{
		std::vector<std::unique_ptr<asio::ip::tcp::acceptor>> reservations;
		for (unsigned i = 0; i < voters; ++i) {
			reservations.push_back(std::make_unique<asio::ip::tcp::acceptor>(io, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0)));
			endpoints.push_back(reservations.back()->local_endpoint());
		}
	}
	std::map<c::NodeId, t::Fingerprint> pins;
	for (unsigned i = 1; i <= voters; ++i) { pins.emplace(i, pin(certificates / ("n" + std::to_string(i) + ".pem"))); }

	const auto before = probe::sample();
	std::vector<std::shared_ptr<t::Peers<Member>>> peers;
	for (unsigned i = 1; i <= voters; ++i) {
		t::PeerOptions options;
		options.tls = {(certificates / ("n" + std::to_string(i) + ".pem")).string(), (certificates / ("n" + std::to_string(i) + ".key")).string(),
			(certificates / "ca.pem").string()};
		options.pins = pins;
		options.listen = endpoints[i - 1];
		for (unsigned k = 1; k <= voters; ++k) { if (k != i) { options.addresses.emplace(k, endpoints[k - 1]); } }
		t::PeerConfiguration configuration;
		configuration.configuration = base;
		configuration.configuration.local = i;
		configuration.cluster[0] = 'B';
		configuration.group[0] = 'P';
		configuration.epoch[0] = 'E';
		peers.push_back(std::make_shared<t::Peers<Member>>(io.get_executor(), members[i - 1], configuration, options));
	}
	const auto constructed = probe::sample();
	for (auto& p : peers) { p->start(); }
	const std::size_t want = 2 * (voters - 1);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
	auto connected = [&] {
		for (auto& p : peers) {
			const auto o = p->occupancy();
			if (o.connections != want || o.handshakes) { return false; }
		}
		return true;
	};
	while (!connected() && std::chrono::steady_clock::now() < deadline) { io.run_for(std::chrono::milliseconds(10)); }
	if (!connected()) { throw std::runtime_error("mesh did not connect"); }
	io.run_for(std::chrono::milliseconds(200));  // idle: nothing to send, every frame suspended
	if (!connected()) { throw std::runtime_error("mesh lost a connection"); }
	const auto idle = probe::sample();
	const double n = voters;
	std::uint64_t reconnects = 0;
	for (auto& p : peers) { reconnects += p->stats().reconnects; }
	if (print) std::printf("peers voters=%u connections_per_member=%zu sizeof_peers=%zu construct_heap_per_member=%.0f construct_fds_per_member=%.2f "
				"idle_heap_per_member=%.0f idle_blocks_per_member=%.1f idle_fds_per_member=%.2f threads_added=%d reconnects=%llu\n",
		voters, want, sizeof(t::Peers<Member>), (constructed.heap - before.heap) / n, (constructed.fds - before.fds) / n,
		(idle.heap - before.heap) / n, (idle.blocks - before.blocks) / n, (idle.fds - before.fds) / n, idle.threads - before.threads,
		static_cast<unsigned long long>(reconnects));
	std::fflush(stdout);
	for (auto& p : peers) { p->stop(); }
	auto drained = [&] {
		for (auto& p : peers) { if (!p->drained()) { return false; } }
		return true;
	};
	const auto stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!drained() && std::chrono::steady_clock::now() < stop_deadline) { io.run_for(std::chrono::milliseconds(10)); }
	peers.clear();
	io.run_for(std::chrono::milliseconds(50));
}
}  // namespace

int main(int argc, char** argv) {
	if (!CRYPTO_set_mem_functions(ssl_malloc, ssl_realloc, ssl_free)) { throw std::runtime_error("OpenSSL allocator already in use"); }
	if (argc < 2) {
		std::fprintf(stderr, "usage: peers_cost <certificate dir> [voters...]\n");
		return 2;
	}
	probe::raise_descriptor_limit();
	const fs::path root = fs::path(std::getenv("BENCH_DIR") ? std::getenv("BENCH_DIR") : "bench-data") / "peers";
	fs::remove_all(root);
	fs::create_directories(root);
	std::vector<unsigned> sizes;
	for (int i = 2; i < argc; ++i) { sizes.push_back(static_cast<unsigned>(std::atoi(argv[i]))); }
	if (sizes.empty()) { sizes = {3, 5}; }
	std::printf("openssl=%s\n", OpenSSL_version(OPENSSL_VERSION));
	group(argv[1], root, 3, false);  // warm-up, not reported: OpenSSL's and Asio's one-time state
	for (unsigned voters : sizes) { group(argv[1], root, voters); }
	fs::remove_all(root);
	std::printf("peers_cost: ok\n");
	return 0;
}
