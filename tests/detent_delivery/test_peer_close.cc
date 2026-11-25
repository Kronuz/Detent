// peer_closed sees a close that follows a byte the peer still had in flight.
#include "peer_close.hh"
#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>
#include <iostream>
#include <thread>

int main() {
	asio::io_context io;
	asio::ip::tcp::acceptor acceptor(io, {asio::ip::address_v4::loopback(), 0});
	std::thread client([&] {
		asio::ip::tcp::socket socket(io);
		socket.connect(acceptor.local_endpoint());
		asio::write(socket, asio::buffer("x", 1));
	});
	asio::ip::tcp::socket accepted(io);
	acceptor.accept(accepted);
	client.join();
	std::size_t stray = 0;
	const bool closed = peer_closed(accepted, std::chrono::seconds(5), stray);
	if (!closed || stray != 1) {
		std::cerr << "peer_closed closed=" << closed << " stray=" << stray << '\n';
		return 1;
	}
	std::cout << "a close after an in-flight byte is seen\n";
}
