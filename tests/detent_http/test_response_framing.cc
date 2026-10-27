#include "detent_http/http_client.hh"
#include <arpa/inet.h>
#include <chrono>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace std::chrono_literals;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("HTTP response framing invariant failed");
}
class Server {
  public:
	explicit Server(std::string response) : response_(std::move(response)) {
		fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
		check(fd_ >= 0);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		check(::bind(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
		socklen_t size = sizeof(address);
		check(::getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &size) == 0);
		port_ = ntohs(address.sin_port);
		check(::listen(fd_, 4) == 0);
		thread_ = std::jthread([this](std::stop_token stop) {
			while (!stop.stop_requested()) {
				pollfd event{fd_, POLLIN, 0};
				if (::poll(&event, 1, 20) <= 0)
					continue;
				auto client = ::accept(fd_, nullptr, nullptr);
				if (client < 0)
					return;
				timeval timeout{1, 0};
				::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#if defined(SO_NOSIGPIPE)
				int one = 1;
				::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
				std::string request;
				char buffer[4096];
				while (request.find("\r\n\r\n") == std::string::npos) {
					auto n = ::recv(client, buffer, sizeof(buffer), 0);
					if (n <= 0)
						break;
					request.append(buffer, n);
				}
				std::size_t offset = 0;
				while (offset < response_.size()) {
#if defined(MSG_NOSIGNAL)
					constexpr int flags = MSG_NOSIGNAL;
#else
					constexpr int flags = 0;
#endif
					auto n = ::send(client, response_.data() + offset, response_.size() - offset, flags);
					if (n <= 0)
						break;
					offset += n;
				}
				::shutdown(client, SHUT_WR);
				::close(client);
			}
		});
	}
	~Server() {
		thread_.request_stop();
		thread_.join();
		::close(fd_);
	}
	std::uint16_t port() const { return port_; }

  private:
	int fd_ = -1;
	std::uint16_t port_ = 0;
	std::string response_;
	std::jthread thread_;
};
void verify(const std::string &wire, bool valid, std::string_view body = {}, int status = 200,
			bool patch = false) {
	static unsigned case_number = 0;
	++case_number;
	Server server(wire);
	for (bool persistent : {false, true}) {
		bool rejected = false;
		detent_http::HttpResponse response;
		try {
			if (persistent) {
				detent_http::HttpConnection connection("127.0.0.1", server.port(), 1s);
				response = connection.get("/");
			} else {
				response = detent_http::http_get("127.0.0.1", server.port(), "/", 1s);
			}
		} catch (const std::exception &) {
			rejected = true;
		}
		if (rejected == valid)
			throw std::runtime_error("HTTP framing case " + std::to_string(case_number) + " unexpected acceptance or rejection");
		if (valid)
			check(response.status == status && response.body == body && response.is_patch == patch);
	}
}
} // namespace
int main() {
	try {
		for (auto coding : {"br", "gzip, identity", "identity, gzip", "zstd, gzip", ""})
			verify(std::string("HTTP/1.1 200 OK\r\nContent-Encoding: ") + coding +
					   "\r\nContent-Length: 3\r\n\r\nabc",
				   false);
		verify("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Encoding: identity\r\nContent-Length: "
			   "3\r\n\r\nabc",
			   false);
		for (auto coding : {"identity", "IDENTITY"})
			verify(std::string("HTTP/1.1 200 OK\r\nContent-Encoding: ") + coding +
					   "\r\nContent-Length: 3\r\n\r\nabc",
				   true, "abc");
		verify("HTTP/1.1 304 Not Modified\r\nContent-Encoding: br\r\nContent-Length: 3\r\n\r\n", true, {},
			   304);
		constexpr char gzip[] =
			"\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff\x4b\x4c\x4a\x06\x00\xc2\x41\x24\x35\x03\x00\x00\x00";
		verify(std::string("HTTP/1.1 200 OK\r\nContent-Encoding: GZIP\r\nContent-Length: 23\r\n\r\n") +
				   std::string(gzip, sizeof(gzip) - 1),
			   true, "abc");
		for (auto type : {"application/merge-patch+json-seq", "application/merge-patch",
						  "application/merge-patchjunk", "application/json"})
			verify(std::string("HTTP/1.1 200 OK\r\nContent-Type: ") + type +
					   "\r\nContent-Length: 3\r\n\r\nabc",
				   true, "abc");
		for (auto type : {"application/merge-patch+json", "Application/Merge-Patch+Json; charset=utf-8",
						  "application/merge-patch+json \t; charset=utf-8"})
			verify(std::string("HTTP/1.1 200 OK\r\nContent-Type: ") + type +
					   "\r\nContent-Length: 3\r\n\r\nabc",
				   true, "abc", 200, true);
		for (auto field :
			 {"Broken-Header", "Content-Length : 3", " Content-Length: 3", "\tFolded: value", ": empty-name",
			  "Bad(Name): value", "X-Test: a\rb", "X-Test: a\nb", "X-Test: a\x01", "X-Test: a\x7f"})
			verify(std::string("HTTP/1.1 200 OK\r\n") + field + "\r\nContent-Length: 3\r\n\r\nabc", false);
		verify("HTTP/1.1 200 OK\r\n!#$%&'*+-.^_`|~: \tvalue\t \r\nContent-Length: 3\r\n\r\nabc", true, "abc");
		for (auto status : {"HTTP/1.1 200junk OK", "HTTP/1.1 +200 OK", "HTTP/1.1 20 OK", "HTTP/1.1 2000 OK",
							"HTTP/1.1 099 Invalid", "HTTP/1.1 600 Invalid", "HTTP/1.1 200",
							"HTTP/1.1 200 OK\t\x01", "HTTP/2.0 200 OK", "garbage 200 OK"})
			verify(std::string(status) + "\r\nContent-Length: 3\r\n\r\nabc", false);
		verify("HTTP/1.1 200 \r\nContent-Length: 3\r\n\r\nabc", true, "abc");
		for (auto length : {"-1", "+3", "3junk", "", "184467440737095516160"})
			verify(std::string("HTTP/1.1 200 OK\r\nContent-Length: ") + length + "\r\n\r\nabc", false);
		verify("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\nabc", false);
		verify("HTTP/1.1 200 OK\r\nContent-Length: 3\r\ncontent-length: 3\r\n\r\nabc", true, "abc");
		verify("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nab", false);
		verify("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nabc", false);
		verify("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n", true, "abc");
		verify("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\nabc", false);
		verify("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nabc", true, "abc");
		verify("HTTP/1.0 200 OK\r\n\r\nabc", true, "abc");
		verify("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", true);
		verify("HTTP/1.1 304 Not Modified\r\nContent-Length: 3\r\nContent-Encoding: zstd\r\n\r\n", true, {},
			   304);
		verify("HTTP/1.1 204 No Content\r\n\r\n", true, {}, 204);
		verify("HTTP/1.1 304 Not Modified\r\n\r\nabc", false);
		std::cout << "HTTP response framing invariants passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
