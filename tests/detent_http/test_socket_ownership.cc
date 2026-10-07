#include "detent_http/http_client.hh"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <new>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// A malformed-response exception allocates its diagnostic. Reuse the client
// descriptor if it was already closed at that point; a second close must not
// destroy this unrelated descriptor. The hook is active only on the caller.
thread_local bool probe_enabled = false;
int expected_socket = -1, replacement = -1;
void *operator new(std::size_t size) {
	if (probe_enabled && expected_socket >= 0 && ::fcntl(expected_socket, F_GETFD) == -1) {
		probe_enabled = false;
		replacement = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
	}
	if (auto *pointer = std::malloc(size ? size : 1))
		return pointer;
	throw std::bad_alloc();
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void check(bool value) {
	if (!value)
		throw std::runtime_error("HTTP socket ownership invariant failed");
}
struct Listener {
	int fd;
	std::uint16_t port = 0;
	std::thread thread;
	explicit Listener(std::size_t body_bytes) : fd(::socket(AF_INET, SOCK_STREAM, 0)) {
		check(fd >= 0);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		check(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
		socklen_t size = sizeof(address);
		check(::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size) == 0);
		port = ntohs(address.sin_port);
		check(::listen(fd, 1) == 0);
		thread = std::thread([this, body_bytes] {
			auto connection = ::accept(fd, nullptr, nullptr);
			if (connection < 0)
				return;
			char buffer[4096];
			std::string request;
			std::size_t end = std::string::npos;
			while (end == std::string::npos || request.size() < end + 4 + body_bytes) {
				auto n = ::recv(connection, buffer, sizeof(buffer), 0);
				if (n <= 0) {
					::close(connection);
					return;
				}
				request.append(buffer, n);
				end = request.find("\r\n\r\n");
			}

			constexpr char response[] = "malformed";
			::send(connection, response, sizeof(response) - 1, 0);
			::shutdown(connection, SHUT_WR);
			::close(connection);
		});
	}
	~Listener() {
		thread.join();
		::close(fd);
	}
};
int main() {
	try {
		for (unsigned method = 0; method < 3; ++method) {
			Listener listener(method == 0 ? 0 : 2);
			auto probe = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
			check(probe >= 0);
			expected_socket = probe;
			::close(probe);
			replacement = -1;
			bool rejected = false;
			probe_enabled = true;
			try {
				if (method == 0)
					(void)detent_http::http_get("127.0.0.1", listener.port, "/",
												std::chrono::milliseconds(1000));
				else if (method == 1)
					(void)detent_http::http_put("127.0.0.1", listener.port, "/", "{}",
												std::chrono::milliseconds(1000));
				else
					(void)detent_http::http_patch("127.0.0.1", listener.port, "/", "{}",
												  std::chrono::milliseconds(1000));
			} catch (const std::exception &error) {
				rejected =
					std::string_view(error.what()).find("no header terminator") != std::string_view::npos;
			}
			probe_enabled = false;
			bool intact = replacement < 0 || ::fcntl(replacement, F_GETFD) != -1;
			if (replacement >= 0 && intact)
				::close(replacement);
			check(rejected && intact);
		}
		std::cout << "HTTP malformed responses preserve descriptor ownership\n";
		return 0;
	} catch (const std::exception &error) {
		probe_enabled = false;
		std::cerr << error.what() << '\n';
		return 1;
	}
}
