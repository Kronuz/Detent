#include "detent_relay/http_upstream.hh"
#include "detent_delivery/namespace_session.hh"
#include "detent_distribution/framing.hh"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <http_parser.h>
#include <map>
#include <rapidjson/document.h>
#include <reactor_tls.h>

namespace detent::relay {
namespace d = distribution;
namespace {
using Clock = std::chrono::steady_clock;
struct Response {
	unsigned status = 0;
	std::map<std::string, std::string, std::less<>> headers; // lowercase names
	std::string body;
	std::string_view header(std::string_view name) const {
		const auto found = headers.find(name);
		return found == headers.end() ? std::string_view{} : std::string_view(found->second);
	}
};
std::string json_string(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\')
			out += '\\';
		if (static_cast<unsigned char>(c) < 0x20)
			throw std::invalid_argument("relay upstream: a control character in a name");
		out += c;
	}
	return out + '"';
}
d::Identity identity(std::string_view text) {
	d::Identity out{};
	auto digit = [](char c) -> int {
		return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
	};
	if (text.size() != 2 * out.size())
		throw UpstreamUnavailable("relay upstream: a session identity of another length");
	for (std::size_t i = 0; i < out.size(); ++i) {
		const int high = digit(text[2 * i]), low = digit(text[2 * i + 1]);
		if (high < 0 || low < 0)
			throw UpstreamUnavailable("relay upstream: a session identity that is not hex");
		out[i] = static_cast<char>(high << 4 | low);
	}
	return out;
}
} // namespace

struct HttpUpstream::Impl {
	HttpUpstreamOptions options;
	std::size_t endpoint = 0;
	std::optional<delivery::NamespaceSession> session;
	Stats stats;

	// One request on its own connection, every step bounded by one deadline.
	Response exchange(std::string_view path, std::string_view body) {
		++stats.requests;
		try {
			return attempt(path, body);
		} catch (...) {
			++stats.failures;
			rotate();
			throw;
		}
	}
	void rotate() {
		session.reset();
		if (options.endpoints.size() > 1) {
			endpoint = (endpoint + 1) % options.endpoints.size();
			++stats.rotations;
		}
	}
	Response attempt(std::string_view path, std::string_view body) {
		asio::io_context io;
		asio::ssl::stream<asio::ip::tcp::socket> socket(io, *options.tls);
		reactor::tls::bind_hostname(socket.native_handle(), options.hostname);
		const auto deadline = Clock::now() + options.timeout;
		// Runs one asynchronous step to completion or the deadline.
		auto run = [&](auto start) {
			std::optional<asio::error_code> result;
			std::size_t transferred = 0;
			start([&](asio::error_code error, std::size_t n = 0) {
				result = error;
				transferred = n;
			});
			io.restart();
			while (!result)
				if (!io.run_one_until(deadline)) {
					asio::error_code ignored;
					socket.lowest_layer().close(ignored);
					io.restart();
					io.run();
					throw UpstreamUnavailable("relay upstream: deadline");
				}
			return std::pair{*result, transferred};
		};
		const auto &at = options.endpoints.at(endpoint);
		if (auto [error, n] = run([&](auto done) {
				socket.lowest_layer().async_connect(at, [done](asio::error_code e) mutable { done(e); });
			});
			error)
			throw UpstreamUnavailable("relay upstream: connect: " + error.message());
		if (auto [error, n] = run([&](auto done) {
				socket.async_handshake(asio::ssl::stream_base::client,
									   [done](asio::error_code e) mutable { done(e); });
			});
			error || SSL_get_verify_result(socket.native_handle()) != X509_V_OK)
			throw UpstreamUnavailable("relay upstream: TLS handshake");
		const auto request = "POST " + std::string(path) + " HTTP/1.1\r\nHost: " + options.hostname +
							 "\r\nAuthorization: Bearer " + options.token +
							 "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
							 std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
		if (auto [error, n] = run([&](auto done) {
				asio::async_write(socket, asio::buffer(request),
								  [done](asio::error_code e, std::size_t m) mutable { done(e, m); });
			});
			error)
			throw UpstreamUnavailable("relay upstream: write: " + error.message());
		struct Parse {
			Response response;
			std::string field;
			std::size_t maximum = 0;
			bool complete = false, overflow = false;
		} parse{{}, {}, options.maximum_bytes};
		http_parser parser;
		http_parser_init(&parser, HTTP_RESPONSE);
		parser.data = &parse;
		http_parser_settings settings{};
		settings.on_header_field = [](http_parser *p, const char *at, std::size_t n) {
			auto &s = *static_cast<Parse *>(p->data);
			s.field.assign(at, n);
			for (auto &c : s.field)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return 0;
		};
		settings.on_header_value = [](http_parser *p, const char *at, std::size_t n) {
			auto &s = *static_cast<Parse *>(p->data);
			s.response.headers[s.field].append(at, n);
			return 0;
		};
		settings.on_body = [](http_parser *p, const char *at, std::size_t n) {
			auto &s = *static_cast<Parse *>(p->data);
			if (n > s.maximum - s.response.body.size()) {
				s.overflow = true;
				return 1;
			}
			s.response.body.append(at, n);
			return 0;
		};
		settings.on_message_complete = [](http_parser *p) {
			static_cast<Parse *>(p->data)->complete = true;
			http_parser_pause(p, 1);
			return 0;
		};
		std::array<char, 16384> input;
		while (!parse.complete) {
			auto [error, n] = run([&](auto done) {
				socket.async_read_some(asio::buffer(input),
									   [done](asio::error_code e, std::size_t m) mutable { done(e, m); });
			});
			const auto consumed = http_parser_execute(&parser, &settings, input.data(), n);
			if (parse.overflow)
				throw UpstreamUnavailable("relay upstream: a response above its bound");
			if (consumed != n || (HTTP_PARSER_ERRNO(&parser) != HPE_OK && HTTP_PARSER_ERRNO(&parser) != HPE_PAUSED))
				throw UpstreamUnavailable("relay upstream: invalid HTTP");
			if (!parse.complete && (error || !n)) {
				http_parser_execute(&parser, &settings, nullptr, 0);
				if (!parse.complete)
					throw UpstreamUnavailable("relay upstream: the response was cut");
			}
		}
		parse.response.status = parser.status_code;
		return std::move(parse.response);
	}
	Response expect(std::string_view path, std::string_view body, unsigned status, std::string_view type) {
		auto response = exchange(path, body);
		if (response.status != status || response.header("content-type") != type) {
			rotate();
			throw UpstreamUnavailable("relay upstream: " + std::string(path) + " answered " +
									  std::to_string(response.status) + ": " + response.body.substr(0, 256));
		}
		return response;
	}
	delivery::NamespaceSession open(const delivery::Following &following) {
		const auto response =
			expect("/v2/distribution/session/open",
				   delivery::namespace_session_body(delivery::SessionResponsePurpose::Open, following, options.compression),
				   200, "application/json");
		rapidjson::Document json;
		json.Parse(response.body.data(), response.body.size());
		if (json.HasParseError() || !json.IsObject() || !json.HasMember("session") || !json["session"].IsString() ||
			!json.HasMember("wait_ms") || !json["wait_ms"].IsUint64())
			throw UpstreamUnavailable("relay upstream: an invalid session opening");
		++stats.sessions;
		return {identity({json["session"].GetString(), json["session"].GetStringLength()}),
				std::chrono::milliseconds(json["wait_ms"].GetUint64())};
	}
	// The fragments' payloads, in order: the W6 body.
	std::string defragment(const Response &response) {
		const auto coding = response.header("x-detent-coding");
		if (response.header("content-type") != "application/vnd.detent.frames" ||
			response.header("x-detent-session-version") != "2" || coding != (options.compression ? "zstd" : "raw"))
			throw UpstreamUnavailable("relay upstream: a frames answer of another shape");
		d::FragmentDecoder decoder(d::UpdateKind::Frames, options.maximum_bytes, options.compression);
		auto input = std::span<const char>(response.body.data(), response.body.size());
		std::string body;
		while (!input.empty()) {
			d::WorkBudget work{input.size() + d::frame_header_bytes, 4};
			decoder.feed(input, work);
			if (auto fragment = decoder.take())
				body.append(fragment->bytes());
		}
		decoder.finish();
		return body;
	}
};

HttpUpstream::HttpUpstream(HttpUpstreamOptions options) : impl_(std::make_unique<Impl>()) {
	if (options.endpoints.empty() || !options.tls || options.hostname.empty() || options.token.empty() ||
		options.token.find_first_of("\r\n") != std::string::npos ||
		options.hostname.find_first_of("\r\n") != std::string::npos)
		throw std::invalid_argument("relay upstream options");
	impl_->options = std::move(options);
}
HttpUpstream::~HttpUpstream() = default;
HttpUpstream::Stats HttpUpstream::stats() const noexcept { return impl_->stats; }

std::vector<NamespaceLayout> HttpUpstream::layouts(std::span<const std::string> names) {
	std::string body = "{\"namespaces\":[";
	for (std::size_t i = 0; i < names.size(); ++i)
		body += (i ? "," : "") + json_string(names[i]);
	body += "]}";
	const auto response = impl_->expect("/v2/history/layouts", body, 200, "application/vnd.detent.layouts");
	try {
		return decode_mirror_table(response.body).entries;
	} catch (const MirrorRefused &error) {
		impl_->rotate();
		throw UpstreamUnavailable(std::string("relay upstream: ") + error.what());
	}
}

std::string HttpUpstream::fetch(std::string_view name_space, const MirrorFile &file) {
	auto response = impl_->expect("/v2/history/file",
								  "{\"namespace\":" + json_string(name_space) + ",\"name\":" + json_string(file.name) + "}", 200,
								  "application/octet-stream");
	++impl_->stats.fetched_files;
	impl_->stats.fetched_bytes += response.body.size();
	return std::move(response.body);
}

std::string HttpUpstream::frames(std::span<const Position> positions) {
	auto &p = *impl_;
	delivery::Following following;
	for (const auto &position : positions)
		following.namespaces.push_back({position.name, position.cursor});
	for (int attempt = 0;; ++attempt) {
		if (!p.session)
			p.session = p.open(following);
		const auto body = delivery::namespace_session_body(delivery::SessionResponsePurpose::Next, following,
														   p.options.compression, p.session->id, {});
		auto response = p.exchange("/v2/distribution/session/next", body);
		if (response.status == 204)
			return d::encode_session_frames(std::nullopt, {});
		if (response.status == 200)
			return p.defragment(response);
		p.session.reset();
		if (response.status != 410 || attempt)
			throw UpstreamUnavailable("relay upstream: next answered " + std::to_string(response.status) + ": " +
									  response.body.substr(0, 256));
	}
}
} // namespace detent::relay
