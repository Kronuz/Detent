#pragma once
#include <array>
#include <asio/buffer.hpp>
#include <asio/error_code.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <poll.h>
#include <string>

// Waits until the peer's close reaches the stream: reads whatever the peer still had in flight and
// returns true at the first read error (end of stream or reset); false when no close arrives in
// `budget`. `stray` counts the bytes read before the close. A close cannot be told from a final
// byte by one read, so a single read_some after poll proves nothing.
template <class Stream>
bool peer_closed(Stream &stream, std::chrono::milliseconds budget, std::size_t &stray,
				std::string *captured = nullptr) {
	const auto deadline = std::chrono::steady_clock::now() + budget;
	for (;;) {
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
		pollfd descriptor{stream.lowest_layer().native_handle(), POLLIN, 0};
		if (left <= std::chrono::milliseconds::zero() || ::poll(&descriptor, 1, static_cast<int>(left.count())) <= 0)
			return false;
		std::array<char, 4096> bytes;
		asio::error_code error;
		const auto got = stream.read_some(asio::buffer(bytes), error);
#ifdef PEER_CLOSE_SINGLE_READ
		stray += error ? 0 : got;
		return bool(error);
#else
		if (error)
			return true;
		stray += got;
		if (captured && captured->size() < 64)
			captured->append(bytes.data(), std::min<std::size_t>(got, 64 - captured->size()));
#endif
	}
}

// Prints the stray bytes (count, hex and printable ASCII of the first 64) and fails when any arrived.
inline void report_stray(const char *where, std::size_t stray, const std::string &bytes) {
	if (!stray)
		return;
	static constexpr char digits[] = "0123456789abcdef";
	std::string hex, text;
	for (unsigned char byte : bytes) {
		hex += digits[byte >> 4];
		hex += digits[byte & 15];
		hex += ' ';
		text += byte >= 32 && byte < 127 ? static_cast<char>(byte) : '.';
	}
	std::cerr << "STRAY " << where << " count=" << stray << " hex=" << hex << "text=" << text << '\n';
}
