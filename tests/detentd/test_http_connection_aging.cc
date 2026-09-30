// Proof for HttpConnection's max_connection_age: a long-lived keep-alive
// connection normally resolves DNS/connects exactly once and reuses that
// one socket forever (see http_client.hh's own design rationale). With
// max_connection_age set, it should instead proactively reconnect (a fresh
// accept() on the server side) once that long, even though the connection
// is still healthy. A minimal hand-rolled TCP listener counts accepts
// directly -- detentd itself doesn't expose a connection-level counter
// (only a request-level one, which a reused keep-alive connection could
// satisfy many times over without ever reconnecting, so it couldn't tell
// these two cases apart).
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>

#include "detent_http/http_client.hh"

using namespace std::chrono_literals;

namespace {

// Serially accepts one connection at a time (this test only ever drives
// one HttpConnection, never concurrently) and serves requests on it,
// WITHOUT closing after each response -- Connection: keep-alive, so the
// only thing that can end a connection is the CLIENT closing its end
// (which max_connection_age's proactive-reconnect path does). Counts
// accepts, which is exactly "how many times did a fresh connection form".
struct CountingListener {
	int listen_fd = -1;
	unsigned short port = 0;
	std::atomic<int> accepts{0};
	std::atomic<bool> stop{false};
	std::thread thread;

	CountingListener() {
		listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
		int one = 1;
		::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr.sin_port = 0; // ask the OS for a free port
		::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
		socklen_t len = sizeof(addr);
		::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len);
		port = ntohs(addr.sin_port);
		::listen(listen_fd, 16);
		thread = std::thread([this] { run(); });
	}

	void run() {
		while (!stop.load()) {
			timeval tv{0, 100'000}; // 100ms poll, so stop() is noticed promptly
			fd_set rfds;
			FD_ZERO(&rfds);
			FD_SET(listen_fd, &rfds);
			int ready = ::select(listen_fd + 1, &rfds, nullptr, nullptr, &tv);
			if (ready <= 0) continue;
			int conn = ::accept(listen_fd, nullptr, nullptr);
			if (conn < 0) continue;
			accepts.fetch_add(1);
			serve_until_closed(conn);
			::close(conn);
		}
	}

	// One accepted connection can carry many sequential requests -- read
	// and answer each until the client closes its end (recv returns <= 0)
	// or the listener itself is stopping.
	void serve_until_closed(int conn) {
		static constexpr std::string_view kResponse =
			"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nok";
		timeval tv{0, 200'000};
		::setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		char buf[4096];
		while (!stop.load()) {
			auto n = ::recv(conn, buf, sizeof(buf), 0);
			if (n == 0) return; // client closed -- exactly the signal we're counting
			if (n < 0) continue; // timeout; loop back and check stop again
			(void)::send(conn, kResponse.data(), kResponse.size(), 0);
		}
	}

	~CountingListener() {
		stop.store(true);
		thread.join();
		::close(listen_fd);
	}
};

} // namespace

int main() {
	using namespace detent_http;

	{
		CountingListener server;
		HttpConnection conn("127.0.0.1", server.port, 2s); // max_connection_age disabled (default)
		for (int i = 0; i < 4; ++i) {
			auto resp = conn.get("/v1/snapshot");
			assert(resp.status == 200);
			std::this_thread::sleep_for(60ms);
		}
		assert(server.accepts.load() == 1); // one connection, reused for all 4 requests
		std::puts("max_connection_age disabled (default): one connection reused across polls OK");
	}

	{
		CountingListener server;
		// 20ms age, 60ms between polls: every single get() call should
		// find its connection already past the age limit and reconnect
		// proactively BEFORE issuing the request -- not just eventually,
		// on some failure.
		HttpConnection conn("127.0.0.1", server.port, 2s, /*max_connection_age=*/20ms);
		for (int i = 0; i < 4; ++i) {
			auto resp = conn.get("/v1/snapshot");
			assert(resp.status == 200);
			std::this_thread::sleep_for(60ms);
		}
		assert(server.accepts.load() == 4); // reconnected every time, each still succeeding
		std::puts("max_connection_age=20ms: reconnects every poll, still correct OK");
	}

	return 0;
}
