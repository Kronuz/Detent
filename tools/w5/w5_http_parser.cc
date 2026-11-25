// http-parser through the old and the new pins: a corpus of valid and malformed requests and responses (the
// request-smuggling classes the guide names) fed whole and one byte at a time, in strict and lenient mode, with
// every callback and the final errno recorded. Detent's detent_delivery/response.cc and detent_transport
// parse responses with this library, and the http server parses requests with it.
#include "common.hh"

#include <http_parser.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

struct Recorder {
	std::string events;
	std::string pending_kind;
	std::string pending;
	void flush() {
		if (!pending_kind.empty()) {
			events += pending_kind + "<" + w5::show(pending, 80) + ">";
			pending_kind.clear();
			pending.clear();
		}
	}
	void data(const char* kind, const char* at, std::size_t length) {
		if (pending_kind != kind) {
			flush();
			pending_kind = kind;
		}
		pending.append(at, length);
	}
	void event(const std::string& e) {
		flush();
		events += e;
	}
};

Recorder& rec(http_parser* p) {
	return *static_cast<Recorder*>(p->data);
}

http_parser_settings settings() {
	http_parser_settings s;
	http_parser_settings_init(&s);
	s.on_message_begin = [](http_parser* p) { rec(p).event("B "); return 0; };
	s.on_url = [](http_parser* p, const char* at, std::size_t n) { rec(p).data("U", at, n); return 0; };
	s.on_status = [](http_parser* p, const char* at, std::size_t n) { rec(p).data("S", at, n); return 0; };
	s.on_header_field = [](http_parser* p, const char* at, std::size_t n) { rec(p).data("F", at, n); return 0; };
	s.on_header_value = [](http_parser* p, const char* at, std::size_t n) { rec(p).data("V", at, n); return 0; };
	s.on_headers_complete = [](http_parser* p) {
		char b[160];
		std::snprintf(b, sizeof b, "H(%u.%u m=%s st=%u cl=%llu up=%u ka=%d) ", p->http_major, p->http_minor,
					  p->type == HTTP_REQUEST ? http_method_str(static_cast<http_method>(p->method)) : "-", p->status_code,
					  static_cast<unsigned long long>(p->content_length), p->upgrade, http_should_keep_alive(p));
		rec(p).event(b);
		return 0;
	};
	s.on_body = [](http_parser* p, const char* at, std::size_t n) { rec(p).data("D", at, n); return 0; };
	s.on_message_complete = [](http_parser* p) { rec(p).event("M "); return 0; };
	s.on_chunk_header = [](http_parser* p) { rec(p).event("K" + std::to_string(p->content_length) + " "); return 0; };
	s.on_chunk_complete = [](http_parser* p) { rec(p).event("k "); return 0; };
	return s;
}

std::string run(http_parser_type type, bool lenient, bool bytewise, const std::string& bytes) {
	http_parser parser;
	http_parser_init(&parser, type);
	if constexpr (requires { parser.lenient_http_headers; }) {
		parser.lenient_http_headers = lenient ? 1 : 0;
	} else if (lenient) {
		return "n/a";
	}
	Recorder r;
	parser.data = &r;
	auto s = settings();
	std::size_t parsed = 0;
	if (bytewise) {
		for (std::size_t i = 0; i < bytes.size(); ++i) {
			auto n = http_parser_execute(&parser, &s, bytes.data() + i, 1);
			parsed += n;
			if (n != 1 || parser.http_errno != HPE_OK || parser.upgrade) break;
		}
	} else {
		parsed = http_parser_execute(&parser, &s, bytes.data(), bytes.size());
	}
	// End of input, as a connection close.
	std::size_t eof = 0;
	if (parser.http_errno == HPE_OK && !parser.upgrade) eof = http_parser_execute(&parser, &s, nullptr, 0);
	r.flush();
	char b[96];
	std::snprintf(b, sizeof b, "parsed=%zu/%zu eof=%zu errno=%s up=%u | ", parsed, bytes.size(), eof,
				  http_errno_name(static_cast<http_errno>(parser.http_errno)), parser.upgrade);
	return b + r.events;
}

using Corpus = std::vector<std::pair<std::string, std::string>>;

Corpus requests() {
	const std::string post = "POST /v1/knobs HTTP/1.1\r\nHost: a\r\n";
	Corpus c;
	auto add = [&](const std::string& n, const std::string& s) { c.push_back({n, s}); };
	add("get-plain", "GET /v1/snapshot HTTP/1.1\r\nHost: a\r\nAccept-Encoding: zstd, gzip\r\n\r\n");
	add("get-conditional", "GET /v1/snapshot HTTP/1.1\r\nHost: a\r\nIf-None-Match: \"abc\"\r\n\r\n");
	add("put-cl", "PUT /v1/knobs HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\n\r\nhello");
	add("patch-chunked", "PATCH /v1/knobs HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
	add("pipelined", "GET /a HTTP/1.1\r\nHost: a\r\n\r\nGET /b HTTP/1.1\r\nHost: a\r\n\r\n");
	add("http10", "GET / HTTP/1.0\r\n\r\n");
	add("http10-keepalive", "GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
	add("http10-te", "POST / HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
	add("connection-close", "GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n");
	add("upgrade", "GET / HTTP/1.1\r\nHost: a\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\nxyz");
	add("connect", "CONNECT a:443 HTTP/1.1\r\nHost: a:443\r\n\r\nxyz");
	add("absolute-uri", "GET http://a/b?c=d#e HTTP/1.1\r\nHost: a\r\n\r\n");
	add("asterisk", "OPTIONS * HTTP/1.1\r\nHost: a\r\n\r\n");
	add("unknown-method", "FOO / HTTP/1.1\r\nHost: a\r\n\r\n");
	add("lowercase-method", "get / HTTP/1.1\r\nHost: a\r\n\r\n");
	add("all-methods", "PURGE / HTTP/1.1\r\n\r\nSEARCH / HTTP/1.1\r\n\r\nPROPFIND / HTTP/1.1\r\n\r\nMKCOL / HTTP/1.1\r\n\r\n");
	// Bare LF and CR handling.
	add("bare-lf-request-line", "GET / HTTP/1.1\nHost: a\r\n\r\n");
	add("bare-lf-header", "GET / HTTP/1.1\r\nHost: a\nX: y\r\n\r\n");
	add("bare-lf-end", "GET / HTTP/1.1\r\nHost: a\r\n\n");
	add("lf-only-message", "GET / HTTP/1.1\nHost: a\n\n");
	add("bare-cr", "GET / HTTP/1.1\r\nHost: a\rX: y\r\n\r\n");
	add("cr-in-value", "GET / HTTP/1.1\r\nHost: a\r\nX: y\rz\r\n\r\n");
	// Folding and control bytes.
	add("obs-fold", "GET / HTTP/1.1\r\nHost: a\r\nX: y\r\n  z\r\n\r\n");
	add("obs-fold-tab", "GET / HTTP/1.1\r\nHost: a\r\nX: y\r\n\tz\r\n\r\n");
	add("fold-first-header", "GET / HTTP/1.1\r\n Host: a\r\n\r\n");
	add("ctl-in-value", std::string("GET / HTTP/1.1\r\nHost: a\r\nX: y\x01z\r\n\r\n"));
	add("nul-in-value", std::string("GET / HTTP/1.1\r\nHost: a\r\nX: y\0z\r\n\r\n", 40));
	add("del-in-value", "GET / HTTP/1.1\r\nHost: a\r\nX: y\x7fz\r\n\r\n");
	add("tab-in-value", "GET / HTTP/1.1\r\nHost: a\r\nX: y\tz\r\n\r\n");
	add("8bit-value", "GET / HTTP/1.1\r\nHost: a\r\nX: caf\xc3\xa9\r\n\r\n");
	add("ctl-in-name", std::string("GET / HTTP/1.1\r\nHo\x01st: a\r\n\r\n"));
	add("space-before-colon", "GET / HTTP/1.1\r\nHost : a\r\n\r\n");
	add("no-colon", "GET / HTTP/1.1\r\nHost a\r\n\r\n");
	add("empty-name", "GET / HTTP/1.1\r\n: a\r\n\r\n");
	add("empty-value", "GET / HTTP/1.1\r\nHost:\r\nX:\r\n\r\n");
	add("8bit-name", "GET / HTTP/1.1\r\nH\xc3\xb6st: a\r\n\r\n");
	add("space-in-url", "GET /a b HTTP/1.1\r\nHost: a\r\n\r\n");
	add("ctl-in-url", std::string("GET /a\x01 HTTP/1.1\r\nHost: a\r\n\r\n"));
	add("8bit-url", "GET /caf\xc3\xa9 HTTP/1.1\r\nHost: a\r\n\r\n");
	add("fragment-url", "GET /a#b HTTP/1.1\r\nHost: a\r\n\r\n");
	add("long-url", "GET /" + std::string(70000, 'a') + " HTTP/1.1\r\nHost: a\r\n\r\n");
	add("long-header", "GET / HTTP/1.1\r\nX: " + std::string(70000, 'a') + "\r\n\r\n");
	// Versions.
	for (const char* v : {"HTTP/1.0", "HTTP/1.1", "HTTP/0.9", "HTTP/1.2", "HTTP/2.0", "HTTP/1.", "HTTP/1", "HTTP/", "http/1.1", "HTTP/1.1x", "HTTP/01.1", "HTTP/1.10", "HTTP/9.9", "XHTTP/1.1", "HTTP /1.1", ""}) {
		add(std::string("version ") + v, std::string("GET / ") + v + "\r\nHost: a\r\n\r\n");
	}
	add("no-version", "GET /\r\n\r\n");
	add("two-spaces", "GET  / HTTP/1.1\r\n\r\n");
	// Transfer-Encoding.
	for (const char* te : {"chunked", "Chunked", "CHUNKED", "chunked ", " chunked", "chunked\t", "chunked, identity", "identity, chunked", "gzip, chunked", "gzip", "identity", "chunked;q=1", "xchunked", "chunkedx", "chunked,chunked", "", "chunked\x01"}) {
		add(std::string("te ") + w5::show(te), post + "Transfer-Encoding: " + te + "\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
	}
	add("te-twice", post + "Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
	add("te-then-gzip", post + "Transfer-Encoding: chunked\r\nTransfer-Encoding: gzip\r\n\r\n0\r\n\r\n");
	add("te-and-cl", post + "Transfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n0\r\n\r\n");
	add("cl-and-te", post + "Content-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
	add("te-cl-smuggle", post + "Content-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n5c\r\nGPOST / HTTP/1.1\r\nContent-Length: 15\r\n\r\nx=1\r\n0\r\n\r\n");
	// Content-Length.
	for (const char* cl : {"5", "05", "+5", "-5", "5 ", " 5", "5\t", "0x5", "5,5", "5, 5", "5a", "a5", "", " ", "0", "00", "18446744073709551615", "18446744073709551616", "99999999999999999999999", "4294967296", "2147483648", "-0", "5.0", "1e1"}) {
		add(std::string("cl ") + w5::show(cl), post + "Content-Length: " + cl + "\r\n\r\nhello");
	}
	add("cl-twice-equal", post + "Content-Length: 5\r\nContent-Length: 5\r\n\r\nhello");
	add("cl-twice-different", post + "Content-Length: 5\r\nContent-Length: 6\r\n\r\nhello!");
	add("cl-in-trailer", post + "Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\nContent-Length: 9\r\n\r\n");
	add("trailer-te", post + "Transfer-Encoding: chunked\r\n\r\n0\r\nTransfer-Encoding: chunked\r\n\r\n");
	add("trailer-ok", post + "Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\nX-Sum: 1\r\n\r\n");
	// Chunk lines.
	for (const char* sz : {"5", "05", "0005", "5;ext", "5;ext=val", "5 ;ext", "5 ", " 5", "+5", "-5", "0x5", "g", "5g", "", ";", "FFFFFFFFFFFFFFFF", "10000000000000000", "ffffffffffffffff0", "5\t", "5\r"}) {
		add(std::string("chunk ") + w5::show(sz), post + "Transfer-Encoding: chunked\r\n\r\n" + sz + "\r\nhello\r\n0\r\n\r\n");
	}
	add("chunk-missing-crlf", post + "Transfer-Encoding: chunked\r\n\r\n5\r\nhelloXX0\r\n\r\n");
	add("chunk-bare-lf", post + "Transfer-Encoding: chunked\r\n\r\n5\nhello\n0\n\n");
	add("chunk-data-bare-lf", post + "Transfer-Encoding: chunked\r\n\r\n5\r\nhello\n0\r\n\r\n");
	add("chunk-last-no-trailer-end", post + "Transfer-Encoding: chunked\r\n\r\n0\r\n");
	// Several messages on one connection, the second after a rejected first.
	add("smuggle-bare-lf-then-get", "POST / HTTP/1.1\r\nContent-Length: 6\nX: y\r\n\r\nGET /admin HTTP/1.1\r\n\r\n");
	add("keepalive-then-get", "GET / HTTP/1.1\r\nHost: a\r\n\r\nGET /admin HTTP/1.1\r\nHost: a\r\n\r\n");
	return c;
}

Corpus responses() {
	Corpus c;
	auto add = [&](const std::string& n, const std::string& s) { c.push_back({n, s}); };
	add("ok-cl", "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\nETag: \"a\"\r\n\r\n{\"v\":12345}\n");
	add("ok-zstd", "HTTP/1.1 200 OK\r\nContent-Encoding: zstd\r\nContent-Length: 4\r\n\r\nabcd");
	add("ok-chunked", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
	add("ok-eof-body", "HTTP/1.1 200 OK\r\n\r\nstreamed until close");
	add("ok-eof-body-http10", "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\nbody");
	add("304", "HTTP/1.1 304 Not Modified\r\nETag: \"a\"\r\n\r\n");
	add("304-with-cl", "HTTP/1.1 304 Not Modified\r\nContent-Length: 5\r\n\r\nhello");
	add("204", "HTTP/1.1 204 No Content\r\n\r\n");
	add("204-with-cl", "HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\nhello");
	add("204-with-te", "HTTP/1.1 204 No Content\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
	add("100-continue", "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
	add("100-with-cl", "HTTP/1.1 100 Continue\r\nContent-Length: 5\r\n\r\nhello");
	add("101-upgrade", "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\nxyz");
	add("101-no-upgrade", "HTTP/1.1 101 Switching Protocols\r\n\r\nxyz");
	add("102", "HTTP/1.1 102 Processing\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
	add("te-and-cl", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n0\r\n\r\n");
	add("cl-and-te", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
	add("http10-te", "HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
	add("te-gzip-last", "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nabc");
	add("te-chunked-gzip", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked, gzip\r\n\r\n0\r\n\r\n");
	add("bad-status-2digits", "HTTP/1.1 20 OK\r\n\r\n");
	add("bad-status-4digits", "HTTP/1.1 2000 OK\r\n\r\n");
	add("bad-status-none", "HTTP/1.1 OK\r\n\r\n");
	add("bad-status-letters", "HTTP/1.1 2x0 OK\r\n\r\n");
	add("status-no-reason", "HTTP/1.1 200\r\nContent-Length: 0\r\n\r\n");
	add("status-no-reason-space", "HTTP/1.1 200 \r\nContent-Length: 0\r\n\r\n");
	add("status-bare-lf", "HTTP/1.1 200 OK\nContent-Length: 0\n\n");
	add("status-999", "HTTP/1.1 999 Odd\r\nContent-Length: 0\r\n\r\n");
	add("status-099", "HTTP/1.1 099 Odd\r\nContent-Length: 0\r\n\r\n");
	add("status-ctl-reason", std::string("HTTP/1.1 200 O\x01K\r\nContent-Length: 0\r\n\r\n"));
	add("bad-version", "HTTP/3.0 200 OK\r\nContent-Length: 0\r\n\r\n");
	add("no-version", "200 OK\r\nContent-Length: 0\r\n\r\n");
	add("lower-version", "http/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
	add("cl-bad", "HTTP/1.1 200 OK\r\nContent-Length: 5x\r\n\r\nhello");
	add("cl-sign", "HTTP/1.1 200 OK\r\nContent-Length: +5\r\n\r\nhello");
	add("cl-twice-different", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello!");
	add("obs-fold", "HTTP/1.1 200 OK\r\nX: y\r\n  z\r\nContent-Length: 0\r\n\r\n");
	add("ctl-in-value", std::string("HTTP/1.1 200 OK\r\nX: y\x01z\r\nContent-Length: 0\r\n\r\n"));
	add("bare-lf-header", "HTTP/1.1 200 OK\r\nX: y\nContent-Length: 0\r\n\r\n");
	add("chunk-bad", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n");
	add("chunk-ext", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5;a=b\r\nhello\r\n0\r\n\r\n");
	add("chunk-trailer", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\nX-T: 1\r\n\r\n");
	add("close-delimited-http11", "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody");
	add("two-responses", "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\naHTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
	add("truncated-head", "HTTP/1.1 200 OK\r\nContent-Len");
	add("truncated-body", "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc");
	add("empty", "");
	return c;
}

std::string url_case(const std::string& url, bool connect) {
	http_parser_url u;
	std::memset(&u, 0, sizeof u);
	int rc = http_parser_parse_url(url.data(), url.size(), connect ? 1 : 0, &u);
	std::string out = "rc=" + std::to_string(rc);
	if (rc == 0) {
		out += " set=" + std::to_string(u.field_set) + " port=" + std::to_string(u.port);
		for (int f = 0; f < UF_MAX; ++f) {
			if (u.field_set & (1 << f)) out += " " + std::to_string(f) + "=" + std::to_string(u.field_data[f].off) + "+" + std::to_string(u.field_data[f].len);
		}
	}
	return out;
}

} // namespace

int main() {
	for (auto [type, name, corpus] : {std::tuple{HTTP_REQUEST, "request", requests()}, std::tuple{HTTP_RESPONSE, "response", responses()}}) {
		for (const auto& [id, bytes] : corpus) {
			for (bool lenient : {false, true}) {
				for (bool bytewise : {false, true}) {
					std::string result = w5::isolated([&] { return run(type, lenient, bytewise, bytes); });
					w5::line(std::string(name) + " " + id + (lenient ? " lenient" : " strict") + (bytewise ? " bytewise" : " whole"), result);
				}
			}
		}
	}
	for (const char* url : {"/", "/a/b?c=d", "http://h/p?q#f", "http://u:p@h:80/", "h:443", "http://[::1]:80/", "http://h:99999/", "/a b", "", "*", "/%zz", "http://h", "//h/p", "http://", "/a#b#c", "h:80:90", "/\x01", "/caf\xc3\xa9"}) {
		w5::line(std::string("url ") + w5::show(url), w5::isolated([&] { return url_case(url, false); }));
		w5::line(std::string("url-connect ") + w5::show(url), w5::isolated([&] { return url_case(url, true); }));
	}
	// A buffer length shorter than the URL (the parse_url read past buflen).
	std::string full = "http://host:8080/path?query=1#frag";
	for (std::size_t n = 0; n <= full.size(); ++n) {
		w5::line("url-prefix " + std::to_string(n), w5::isolated([&] { return url_case(full.substr(0, n), false); }));
	}
	for (int e = 0; e <= static_cast<int>(HPE_UNKNOWN); ++e) {
		w5::line("errno " + std::to_string(e), http_errno_name(static_cast<http_errno>(e)));
	}
	return 0;
}
