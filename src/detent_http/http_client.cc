#include "detent_http/http_client.hh"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>

#include <compressor_deflate.h>
#include <compressor_zstd.h>

namespace detent_http {

namespace {

class SocketOwner {
  public:
	explicit SocketOwner(int fd) noexcept : fd_(fd) {}
	~SocketOwner() { ::close(fd_); }
	SocketOwner(const SocketOwner &) = delete;
	SocketOwner &operator=(const SocketOwner &) = delete;
	int get() const noexcept { return fd_; }

  private:
	int fd_;
};

int connect_with_timeout(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout) {
	addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo* result = nullptr;
	auto port_str = std::to_string(port);
	int rc = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
	if (rc != 0 || result == nullptr) {
		throw std::runtime_error("detent_http: DNS resolution failed for " + host + ": " + gai_strerror(rc));
	}

	int fd = -1;
	for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
		fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0) continue;

		timeval tv{};
		tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
		tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
		::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#if defined(SO_NOSIGPIPE)
		// macOS/BSD's per-socket equivalent of Linux's MSG_NOSIGNAL send()
		// flag (see kNoSignalFlag above) -- best-effort, an EINVAL/ENOTSOCK
		// here isn't worth failing the whole connection over.
		int one = 1;
		::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

		if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
		::close(fd);
		fd = -1;
	}
	freeaddrinfo(result);

	if (fd < 0) {
		throw std::runtime_error("detent_http: connect failed to " + host + ":" + port_str);
	}
	return fd;
}

// A peer that closes/resets its end of the connection between our connect()
// and our next send() raises SIGPIPE by default, which terminates the whole
// process -- not just this call -- unless suppressed. Linux/glibc offers
// per-call suppression via MSG_NOSIGNAL; BSD/macOS instead requires the
// per-socket SO_NOSIGPIPE option (set once, at socket-creation time, see
// connect_with_timeout() below). Neither flag exists on the other platform,
// hence the #ifdef rather than relying on just one.
#if defined(MSG_NOSIGNAL)
constexpr int kNoSignalFlag = MSG_NOSIGNAL;
#else
constexpr int kNoSignalFlag = 0;
#endif

void write_all(int fd, std::string_view data) {
	std::size_t sent = 0;
	while (sent < data.size()) {
		auto n = ::send(fd, data.data() + sent, data.size() - sent, kNoSignalFlag);
		if (n > 0) {
			sent += static_cast<std::size_t>(n);
		} else if (n < 0 && errno == EINTR) {
			continue;
		} else {
			throw std::runtime_error(std::string("detent_http: send failed: ") + std::strerror(errno));
		}
	}
}

std::string_view trim(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\r' || s.front() == '\t')) s.remove_prefix(1);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.remove_suffix(1);
	return s;
}

bool iequals(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) return false;
	return std::equal(a.begin(), a.end(), b.begin(),
	                   [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}

struct ParsedHead {
	int status = 0;
	std::optional<std::string> etag;
	std::optional<std::size_t> content_length;
	bool has_close_header = false;
	bool is_patch = false; // Content-Type: application/merge-patch+json
	std::string content_encoding; // "", "zstd", or "gzip" -- see decode_body()
};

// Parses the status line + headers from `head` (everything before the
// "\r\n\r\n" terminator, not including it).
ParsedHead parse_head(std::string_view head) {
	ParsedHead out;
	auto line_end = head.find("\r\n");
	std::string_view status_line = line_end == std::string_view::npos ? head : head.substr(0, line_end);

	if (status_line.size() < 13 ||
		(status_line.substr(0, 8) != "HTTP/1.1" && status_line.substr(0, 8) != "HTTP/1.0") ||
		status_line[8] != ' ' || status_line[9] < '1' || status_line[9] > '5' ||
		status_line[10] < '0' || status_line[10] > '9' ||
		status_line[11] < '0' || status_line[11] > '9' || status_line[12] != ' ' ||
		std::any_of(status_line.begin() + 13, status_line.end(), [](unsigned char byte) {
			return (byte < 32 && byte != '\t') || byte == 127;
		})) {
		throw std::runtime_error("detent_http: malformed or unsupported status line");
	}
	out.status = (status_line[9] - '0') * 100 + (status_line[10] - '0') * 10 + (status_line[11] - '0');

	std::size_t pos = line_end == std::string_view::npos ? head.size() : line_end + 2;
	while (pos < head.size()) {
		auto next = head.find("\r\n", pos);
		auto len = (next == std::string_view::npos ? head.size() : next) - pos;
		std::string_view line = head.substr(pos, len);
		auto colon = line.find(':');
		if (colon != std::string_view::npos) {
			auto name = trim(line.substr(0, colon));
			auto value = trim(line.substr(colon + 1));
			if (iequals(name, "ETag")) {
				out.etag = std::string(value);
			} else if (iequals(name, "Content-Length")) {
				std::size_t length = 0;
				auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
				if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
					(out.content_length && *out.content_length != length)) {
					throw std::runtime_error("detent_http: invalid or conflicting Content-Length");
				}
				out.content_length = length;
			} else if (iequals(name, "Transfer-Encoding")) {
				throw std::runtime_error("detent_http: unsupported Transfer-Encoding");
			} else if (iequals(name, "Connection") && iequals(value, "close")) {
				out.has_close_header = true;
			} else if (iequals(name, "Content-Type")) {
				// Only the media type matters here, not e.g. a "; charset=..."
				// parameter, so a prefix match is enough and avoids parsing
				// full media-type parameter syntax for a value this code
				// only ever compares against one literal.
				static constexpr std::string_view kPatchMediaType = "application/merge-patch";
				out.is_patch = value.substr(0, kPatchMediaType.size()) == kPatchMediaType;
			} else if (iequals(name, "Content-Encoding")) {
				out.content_encoding = std::string(value);
			}
		}
		if (next == std::string_view::npos) break;
		pos = next + 2;
	}
	return out;
}

// Both codings this client ever advertises (see kAcceptEncoding below) are
// whole-body, one-shot formats -- no framing/streaming needed here, unlike
// detentd's own encode() which streams into a growing response buffer.
// content_encoding is whatever the server actually chose, which may be ""
// (identity) even when we advertised support for more, or a coding neither
// of us expected from some other proxy in between -- that last case is
// passed through unchanged rather than guessed at.
std::string decode_body(std::string_view body, std::string_view content_encoding) {
	if (content_encoding == "zstd") {
		return decompress_zstd(body);
	}
	if (content_encoding == "gzip") {
		std::string decompressed;
		DeflateDecompressData decompressor(body.data(), body.size(), /*gzip_=*/true);
		for (auto it = decompressor.begin(); it; ++it) decompressed.append(*it);
		return decompressed;
	}
	return std::string(body);
}

// Sent on every GET: zstd first (detentd's own tie-break preference, see
// http_compression.h's supported_encodings()), gzip as a fallback for an
// older/different server. Never sent on PUT/PATCH -- those responses are a
// few bytes of JSON, not worth negotiating.
constexpr std::string_view kAcceptEncoding = "Accept-Encoding: zstd, gzip\r\n";

std::string read_all_to_eof(int fd) {
	std::string out;
	char buf[16384];
	for (;;) {
		auto n = ::recv(fd, buf, sizeof(buf), 0);
		if (n > 0) {
			out.append(buf, static_cast<std::size_t>(n));
		} else if (n == 0) {
			break;
		} else {
			if (errno == EINTR) continue;
			throw std::runtime_error(std::string("detent_http: recv failed: ") + std::strerror(errno));
		}
	}
	return out;
}

HttpResponse parse_full_response(const std::string& raw) {
	auto header_end = raw.find("\r\n\r\n");
	if (header_end == std::string::npos) {
		throw std::runtime_error("detent_http: truncated/malformed HTTP response (no header terminator)");
	}
	auto head = parse_head(std::string_view(raw).substr(0, header_end));
	auto body = std::string_view(raw).substr(header_end + 4);
	if ((head.status == 204 || head.status == 304) ? !body.empty()
		: (head.content_length && body.size() != *head.content_length)) {
		throw std::runtime_error("detent_http: response body length mismatch");
	}
	HttpResponse resp;
	resp.status = head.status;
	resp.etag = head.etag;
	resp.is_patch = head.is_patch;
	if (head.status != 204 && head.status != 304)
		resp.body = decode_body(body, head.content_encoding);
	return resp;
}

} // namespace

// ---- HttpConnection: reused keep-alive connection --------------------------

HttpConnection::HttpConnection(
	std::string host, std::uint16_t port, std::chrono::milliseconds timeout,
	std::chrono::milliseconds max_connection_age)
	: host_(std::move(host)), port_(port), timeout_(timeout), max_connection_age_(max_connection_age) {}

HttpConnection::~HttpConnection() { disconnect(); }

void HttpConnection::disconnect() noexcept {
	if (fd_ >= 0) {
		::close(fd_);
		fd_ = -1;
	}
}

void HttpConnection::ensure_connected() {
	if (fd_ >= 0 && max_connection_age_.count() != 0 &&
	    std::chrono::steady_clock::now() - connected_at_ >= max_connection_age_) {
		disconnect(); // proactively age out -- the reconnect below re-resolves DNS
	}
	if (fd_ < 0) {
		fd_ = connect_with_timeout(host_, port_, timeout_);
		connected_at_ = std::chrono::steady_clock::now();
	}
}

HttpResponse HttpConnection::get(
	const std::string& path, const std::optional<std::string>& if_none_match,
	std::optional<std::uint64_t> base_version) {
	ensure_connected();
	try {
		return get_once(path, if_none_match, base_version);
	} catch (const std::exception&) {
		// The connection may have gone stale (server-side idle timeout,
		// network blip). Reconnect once and retry before giving up.
		disconnect();
		ensure_connected();
		try {
			return get_once(path, if_none_match, base_version);
		} catch (...) {
			disconnect();
			throw;
		}
	}
}

HttpResponse HttpConnection::get_once(
	const std::string& path, const std::optional<std::string>& if_none_match,
	std::optional<std::uint64_t> base_version) {
	std::string request = "GET " + path + " HTTP/1.1\r\n";
	request += "Host: " + host_ + "\r\n";
	request += "Connection: keep-alive\r\n";
	request += kAcceptEncoding;
	if (if_none_match) {
		request += "If-None-Match: " + *if_none_match + "\r\n";
	}
	if (base_version) {
		request += "X-Detent-Base-Version: " + std::to_string(*base_version) + "\r\n";
	}
	request += "\r\n";
	write_all(fd_, request);

	std::string buf;
	char chunk[16384];
	std::size_t header_end = std::string::npos;
	while (header_end == std::string::npos) {
		auto n = ::recv(fd_, chunk, sizeof(chunk), 0);
		if (n > 0) {
			buf.append(chunk, static_cast<std::size_t>(n));
			header_end = buf.find("\r\n\r\n");
		} else if (n == 0) {
			throw std::runtime_error("detent_http: connection closed before headers completed");
		} else {
			if (errno == EINTR) continue;
			throw std::runtime_error(std::string("detent_http: recv failed: ") + std::strerror(errno));
		}
	}

	auto head = parse_head(std::string_view(buf).substr(0, header_end));
	HttpResponse resp;
	resp.status = head.status;
	resp.etag = head.etag;
	resp.is_patch = head.is_patch;

	std::size_t body_start = header_end + 4;
	// A 204 or 304 is bodyless regardless of any Content-Length header (our
	// own server never sends one for 304, but be defensive about others).
	const bool bodyless = resp.status == 204 || resp.status == 304;
	if (!bodyless && !head.content_length) {
		// A response without an explicit length is delimited by connection
		// closure. Drain it and discard the connection before returning.
		resp.body = buf.substr(body_start);
		resp.body += read_all_to_eof(fd_);
		disconnect();
		resp.body = decode_body(resp.body, head.content_encoding);
		return resp;
	}
	std::size_t want = bodyless ? 0 : *head.content_length;

	resp.body = buf.substr(body_start);
	if (resp.body.size() > want) {
		throw std::runtime_error("detent_http: excess response body bytes");
	}
	resp.body.reserve(want);
	while (resp.body.size() < want) {
		auto n = ::recv(fd_, chunk, std::min(sizeof(chunk), want - resp.body.size()), 0);
		if (n > 0) {
			resp.body.append(chunk, static_cast<std::size_t>(n));
		} else if (n == 0) {
			throw std::runtime_error("detent_http: connection closed mid-body");
		} else {
			if (errno == EINTR) continue;
			throw std::runtime_error(std::string("detent_http: recv failed: ") + std::strerror(errno));
		}
	}

	if (head.has_close_header) {
		disconnect(); // server is ending the connection; the next get() reconnects
	}
	if (!bodyless)
		resp.body = decode_body(resp.body, head.content_encoding);
	return resp;
}

// ---- one-shot helpers (Connection: close, read-to-EOF) --------------------

HttpResponse http_get(
	const std::string& host,
	std::uint16_t port,
	const std::string& path,
	std::chrono::milliseconds timeout,
	const std::optional<std::string>& if_none_match) {
	SocketOwner fd(connect_with_timeout(host, port, timeout));
	std::string request = "GET " + path + " HTTP/1.1\r\n";
	request += "Host: " + host + "\r\n";
	request += "Connection: close\r\n";
	request += kAcceptEncoding;
	if (if_none_match) {
		request += "If-None-Match: " + *if_none_match + "\r\n";
	}
	request += "\r\n";

	write_all(fd.get(), request);
	auto raw = read_all_to_eof(fd.get());
	return parse_full_response(raw);
}

HttpResponse http_put(
	const std::string& host,
	std::uint16_t port,
	const std::string& path,
	std::string body,
	std::chrono::milliseconds timeout) {
	SocketOwner fd(connect_with_timeout(host, port, timeout));
	std::string request = "PUT " + path + " HTTP/1.1\r\n";
	request += "Host: " + host + "\r\n";
	request += "Connection: close\r\n";
	request += "Content-Type: application/json\r\n";
	request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	request += "\r\n";
	request += body;

	write_all(fd.get(), request);
	auto raw = read_all_to_eof(fd.get());
	return parse_full_response(raw);
}

HttpResponse http_patch(
	const std::string& host,
	std::uint16_t port,
	const std::string& path,
	std::string body,
	std::chrono::milliseconds timeout) {
	SocketOwner fd(connect_with_timeout(host, port, timeout));
	std::string request = "PATCH " + path + " HTTP/1.1\r\n";
	request += "Host: " + host + "\r\n";
	request += "Connection: close\r\n";
	request += "Content-Type: application/merge-patch+json\r\n";
	request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	request += "\r\n";
	request += body;

	write_all(fd.get(), request);
	auto raw = read_all_to_eof(fd.get());
	return parse_full_response(raw);
}

} // namespace detent_http
