// Review F12: what one idle peer connection costs, the unit of D10's per-loop transport. Opens
// n mutual-TLS connections over loopback TCP with the context consensus's Peers configures
// (reactor::tls, Role::Peer), completes each handshake, sends one 64-byte frame each way, goes
// idle and prints the heap (operator new and OpenSSL's allocator, usable sizes) and descriptors
// per connection end, three ways:
//   asio      asio::ssl::stream over the socket, as today's Session (session.h:111)
//   direct    OpenSSL on the non-blocking socket (SSL_set_fd), default buffers
//   released  the same with SSL_MODE_RELEASE_BUFFERS: no record buffer while idle
// and the context alone.   conn_cost <certificate dir> [connections]
#include "probe.hh"
PROBE_ALLOCATOR

#include "consensus/transport/tls.h"

#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/ssl.hpp>
#include <asio/write.hpp>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
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

std::shared_ptr<asio::ssl::context> context(const fs::path& certificates, int node) {
	auto c = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
	reactor::tls::configure(*c,
		{(certificates / ("n" + std::to_string(node) + ".pem")).string(), (certificates / ("n" + std::to_string(node) + ".key")).string(),
			(certificates / "ca.pem").string()},
		reactor::tls::Role::Peer);
	return c;
}

// A connected loopback TCP pair: (dialer, acceptor) descriptors.
std::pair<int, int> tcp_pair(int listener, const sockaddr_in& address) {
	const int a = ::socket(AF_INET, SOCK_STREAM, 0);
	if (a < 0 || ::connect(a, reinterpret_cast<const sockaddr*>(&address), sizeof address)) { throw std::runtime_error("connect"); }
	const int b = ::accept(listener, nullptr, nullptr);
	if (b < 0) { throw std::runtime_error("accept"); }
	const int one = 1;
	::setsockopt(a, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	::setsockopt(b, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	return {a, b};
}

struct listener {
	int fd;
	sockaddr_in address{};
	listener() : fd(::socket(AF_INET, SOCK_STREAM, 0)) {
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		socklen_t size = sizeof address;
		if (fd < 0 || ::bind(fd, reinterpret_cast<sockaddr*>(&address), size) || ::listen(fd, 1024) ||
			::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size)) {
			throw std::runtime_error("listen");
		}
	}
	~listener() { ::close(fd); }
};

// Today's shape: asio::ssl::stream ends, handshaken and idle after one frame each way.
struct asio_ends {
	asio::io_context io;
	std::vector<std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>>> ends;
	void open(asio::ssl::context& client, asio::ssl::context& server, listener& l, int n) {
		for (int i = 0; i < n; ++i) {
			auto [a, b] = tcp_pair(l.fd, l.address);
			ends.push_back(std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(asio::ip::tcp::socket(io, asio::ip::tcp::v4(), a), client));
			ends.push_back(std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(asio::ip::tcp::socket(io, asio::ip::tcp::v4(), b), server));
		}
		int failures = 0;
		for (std::size_t i = 0; i < ends.size(); ++i) {
			ends[i]->async_handshake(i % 2 ? asio::ssl::stream_base::server : asio::ssl::stream_base::client, [&](asio::error_code e) { failures += !!e; });
		}
		io.run();
		io.restart();
		static std::array<char, 64> out{}, in{};
		for (std::size_t i = 0; i < ends.size(); ++i) {  // one frame each way
			asio::async_write(*ends[i], asio::buffer(out), [&](asio::error_code e, std::size_t) { failures += !!e; });
			asio::async_read(*ends[i ^ 1], asio::buffer(in), [&](asio::error_code e, std::size_t) { failures += !!e; });
		}
		io.run();
		io.restart();
		if (failures) { throw std::runtime_error("asio handshake or frame"); }
	}
};

// D10's shape: OpenSSL on the non-blocking socket, the loop waiting for readiness.
struct direct_ends {
	std::vector<SSL*> ends;
	std::vector<int> fds;
	void open(SSL_CTX* client, SSL_CTX* server, listener& l, int n, bool release) {
		for (int i = 0; i < n; ++i) {
			auto [a, b] = tcp_pair(l.fd, l.address);
			for (auto [fd, ctx, accept] : {std::tuple{a, client, false}, std::tuple{b, server, true}}) {
				::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
				SSL* s = SSL_new(ctx);
				if (release) { SSL_set_mode(s, SSL_MODE_RELEASE_BUFFERS); }
				SSL_set_fd(s, fd);
				accept ? SSL_set_accept_state(s) : SSL_set_connect_state(s);
				ends.push_back(s);
				fds.push_back(fd);
			}
		}
		for (bool pending = true; pending;) {  // both ends of every pair in one thread
			pending = false;
			for (SSL* s : ends) {
				if (SSL_is_init_finished(s)) { continue; }
				const int r = SSL_do_handshake(s);
				if (r != 1) {
					const int e = SSL_get_error(s, r);
					if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) { throw std::runtime_error("handshake"); }
					pending = true;
				}
			}
		}
		std::array<char, 64> out{}, in{};
		for (std::size_t i = 0; i < ends.size(); ++i) {
			if (SSL_write(ends[i], out.data(), int(out.size())) != int(out.size())) { throw std::runtime_error("write"); }
			for (std::size_t got = 0; got < in.size();) {
				const int r = SSL_read(ends[i ^ 1], in.data() + got, int(in.size() - got));
				if (r > 0) {
					got += std::size_t(r);
				} else if (SSL_get_error(ends[i ^ 1], r) != SSL_ERROR_WANT_READ) {
					throw std::runtime_error("read");
				}
			}
		}
	}
	~direct_ends() {
		for (SSL* s : ends) { SSL_free(s); }
		for (int fd : fds) { ::close(fd); }
	}
};

void report(std::string_view way, int n, const probe::Sample& before, const probe::Sample& idle) {
	const double ends = 2.0 * n;
	std::printf("way=%.*s connections=%d heap_per_end=%.0f blocks_per_end=%.1f fds_per_end=%.2f\n", int(way.size()), way.data(), n,
		(idle.heap - before.heap) / ends, (idle.blocks - before.blocks) / ends, (idle.fds - before.fds) / ends);
	std::fflush(stdout);
}

void measure(const fs::path& certificates, int n, bool print) {
	listener l;
	{
		const auto before = probe::sample();
		auto c = context(certificates, 1);
		const auto after = probe::sample();
		if (print) { std::printf("context heap=%lld blocks=%lld\n", static_cast<long long>(after.heap - before.heap), static_cast<long long>(after.blocks - before.blocks)); }
	}
	auto client = context(certificates, 1), server = context(certificates, 2);
	{
		const auto before = probe::sample();
		asio_ends e;
		e.open(*client, *server, l, n);
		const auto idle = probe::sample();
		if (print) { report("asio", n, before, idle); }
	}
	for (const bool release : {false, true}) {
		const auto before = probe::sample();
		direct_ends e;
		e.open(client->native_handle(), server->native_handle(), l, n, release);
		const auto idle = probe::sample();
		if (print) { report(release ? "released" : "direct", n, before, idle); }
	}
}
}  // namespace

int main(int argc, char** argv) {
	if (!CRYPTO_set_mem_functions(ssl_malloc, ssl_realloc, ssl_free)) { throw std::runtime_error("OpenSSL allocator already in use"); }
	if (argc < 2) {
		std::fprintf(stderr, "usage: conn_cost <certificate dir> [connections]\n");
		return 2;
	}
	probe::raise_descriptor_limit();
	const int n = argc > 2 ? std::atoi(argv[2]) : 64;
	std::printf("openssl=%s\n", OpenSSL_version(OPENSSL_VERSION));
	measure(argv[1], 4, false);  // warm-up: OpenSSL's and Asio's one-time state
	measure(argv[1], n, true);
	std::printf("conn_cost: ok\n");
	return 0;
}
