#include "detent_delivery/namespace_session.hh"
#include "connection_cache_internal.hh"
#include "detent_distribution/framing.hh"
#include "detent_distribution/grants.hh"
#include "detent_transport/await_io.hh"
#include "session_control_internal.hh"
#include <asio.hpp>
#include <charconv>
#include <http_parser.h>
#include <map>
#include <rapidjson/document.h>

namespace detent::delivery {
namespace d = distribution;
namespace {
void require(bool condition, const char *message) {
	if (!condition)
		throw std::invalid_argument(message);
}
void hex(std::string &out, std::span<const char> value) {
	for (unsigned char byte : value) {
		out += "0123456789abcdef"[byte >> 4];
		out += "0123456789abcdef"[byte & 15];
	}
}
std::string hex(std::span<const char> value) {
	std::string out;
	hex(out, value);
	return out;
}
std::uint64_t number(std::string_view value) {
	std::uint64_t result = 0;
	auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
	require(!value.empty() && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(),
			"invalid session response integer");
	return result;
}
d::Identity identity(std::string_view text) {
	d::Identity result{};
	require(text.size() == 2 * result.size(), "invalid session identity length");
	auto digit = [](char c) -> unsigned {
		require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), "invalid session identity digit");
		return c <= '9' ? c - '0' : c - 'a' + 10;
	};
	for (std::size_t i = 0; i < result.size(); ++i)
		result[i] = static_cast<char>((digit(text[2 * i]) << 4) | digit(text[2 * i + 1]));
	require(result != d::Identity{}, "zero session identity");
	return result;
}
} // namespace

std::string namespace_session_body(SessionResponsePurpose purpose, const Following &following, bool compression,
								   d::Identity session, std::chrono::milliseconds wait) {
	std::string body = "{\"version\":2";
	if (purpose != SessionResponsePurpose::Open) {
		require(session != d::Identity{}, "a session request names its session");
		body += ",\"session\":\"" + hex(session) + '"';
		if (purpose == SessionResponsePurpose::Close)
			return body + '}';
	} else {
		body += ",\"profile\":1,\"coding\":\"";
		body += compression ? "zstd\"" : "raw\"";
	}
	d::Identity cluster{};
	if (following.all && following.all->cluster != d::Identity{})
		cluster = following.all->cluster;
	for (const auto &followed : following.namespaces)
		if (cluster == d::Identity{} && followed.cursor)
			cluster = followed.cursor->cluster;
	if (cluster != d::Identity{})
		body += ",\"cluster\":\"" + hex(cluster) + '"';
	body += ",\"namespaces\":[";
	std::string_view previous;
	for (const auto &followed : following.namespaces) {
		require(d::valid_namespace_name(followed.name) && (previous.empty() || previous < followed.name),
				"followed namespaces are not valid names in order");
		if (!previous.empty())
			body += ',';
		previous = followed.name;
		body += "[\"" + followed.name + '"';
		if (const auto &at = followed.cursor; at && at->cluster == cluster && at->incarnation != d::Identity{})
			body += ",\"" + hex(at->incarnation) + "\"," + std::to_string(at->position);
		body += ']';
	}
	body += ']';
	require(!following.namespaces.empty() || following.all, "a session follows at least one namespace or all");
	if (following.all)
		body += ",\"all\":" +
				std::to_string(following.all->cluster == cluster ? following.all->position : std::uint64_t{0});
	if (purpose == SessionResponsePurpose::Next)
		body += ",\"wait_ms\":" + std::to_string(wait.count());
	return body + '}';
}

struct FramesResponseDecoder::Impl {
	SessionResponsePurpose purpose;
	d::Identity session;
	bool compression;
	d::FrameSink *sink;
	d::Limits limits;
	http_parser parser{};
	http_parser_settings settings{};
	std::map<std::string, std::string, std::less<>> headers;
	std::string field, value, control;
	std::size_t header_bytes = 0;
	bool last_value = false, headers_done = false, complete = false;
	std::exception_ptr error;
	std::optional<d::FragmentDecoder> fragments;
	std::optional<d::SessionFramesDecoder> frames;
	std::optional<NamespaceSession> opened;
	std::optional<SessionResetReason> reset;
	FramesOutcome outcome = FramesOutcome::Idle;
	Impl(SessionResponsePurpose kind, d::Identity id, bool coding, d::FrameSink *target, d::Limits bounds)
		: purpose(kind), session(id), compression(coding), sink(target), limits(d::detail::admitted_limits(bounds)) {
		require((purpose == SessionResponsePurpose::Open) == (session == d::Identity{}) &&
					(purpose != SessionResponsePurpose::Next || sink),
				"invalid session response binding");
		http_parser_init(&parser, HTTP_RESPONSE);
		parser.data = this;
		settings.on_header_field = [](http_parser *p, const char *bytes, std::size_t n) {
			return callback(p, [&](Impl &self) {
				if (self.last_value)
					self.commit_header();
				self.admit_header(n);
				for (std::size_t i = 0; i < n; ++i)
					self.field += static_cast<char>(std::tolower(static_cast<unsigned char>(bytes[i])));
			});
		};
		settings.on_header_value = [](http_parser *p, const char *bytes, std::size_t n) {
			return callback(p, [&](Impl &self) {
				self.admit_header(n);
				self.last_value = true;
				self.value.append(bytes, n);
			});
		};
		settings.on_headers_complete = [](http_parser *p) {
			return callback(p, [](Impl &self) { self.admit(); });
		};
		settings.on_body = [](http_parser *p, const char *bytes, std::size_t n) {
			return callback(p, [&](Impl &self) { self.body({bytes, n}); });
		};
		settings.on_message_complete = [](http_parser *p) {
			return callback(p, [](Impl &self) { self.finish(); });
		};
	}
	template <class F> static int callback(http_parser *parser, F &&work) noexcept {
		auto &self = *static_cast<Impl *>(parser->data);
		try {
			work(self);
			return 0;
		} catch (...) {
			if (!self.error)
				self.error = std::current_exception();
			return 1;
		}
	}
	void admit_header(std::size_t n) {
		require(!headers_done && n <= 65536 - header_bytes, "session response header bound");
		header_bytes += n;
	}
	void commit_header() {
		require(!field.empty() && headers.size() < 128 && headers.emplace(field, value).second,
				"duplicate or excessive session response headers");
		field.clear();
		value.clear();
		last_value = false;
	}
	std::string_view header(std::string_view key) const {
		auto found = headers.find(key);
		require(found != headers.end(), "missing session response header");
		return found->second;
	}
	void admit() {
		if (!field.empty())
			commit_header();
		headers_done = true;
		require(parser.http_major == 1 && parser.http_minor == 1, "session response HTTP version");
		const auto status = parser.status_code;
		if (status == 204) {
			require(purpose != SessionResponsePurpose::Open, "an open is answered with its session");
			return;
		}
		if (status == 200 && purpose == SessionResponsePurpose::Next) {
			require(header("content-type") == "application/vnd.detent.frames" &&
						header("x-detent-session-version") == "2" && header("x-detent-session") == hex(session) &&
						header("x-detent-coding") == (compression ? "zstd" : "raw"),
					"session frames response negotiation mismatch");
			fragments.emplace(d::UpdateKind::Frames, limits.bytes, compression, limits.storage_account);
			frames.emplace(*sink, limits);
			outcome = FramesOutcome::Frames;
			return;
		}
		require(status == 200 ? purpose == SessionResponsePurpose::Open
							  : status == 400 || status == 401 || status == 403 || status == 409 || status == 410 ||
									status == 429 || status == 503,
				"session response status rejected");
		require(header("content-type") == "application/json" && !headers.contains("transfer-encoding") &&
					number(header("content-length")) <= 4096,
				"session control body bound or type");
	}
	void body(std::string_view bytes) {
		require(headers_done && parser.status_code != 204, "session response body unexpected");
		if (!fragments) {
			require(bytes.size() <= 4096 - control.size(), "session control body bound");
			control.append(bytes);
			return;
		}
		auto input = std::span<const char>(bytes.data(), bytes.size());
		while (!input.empty()) {
			d::WorkBudget budget{input.size() + d::frame_header_bytes, 4};
			fragments->feed(input, budget);
			if (auto fragment = fragments->take()) {
				auto payload = std::span<const char>(fragment->bytes().data(), fragment->bytes().size());
				std::size_t work = payload.size();
				frames->feed(payload, work);
			}
		}
	}
	void finish() {
		complete = true;
		http_parser_pause(&parser, 1);
		const auto status = parser.status_code;
		if (fragments) {
			fragments->finish();
			frames->finish();
			return;
		}
		if (status == 204)
			return;
		rapidjson::Document json;
		json.Parse<rapidjson::kParseValidateEncodingFlag>(control.data(), control.size());
		require(!json.HasParseError() && json.IsObject(), "invalid session control JSON");
		auto text = [&](const char *key) -> std::string_view {
			require(json.HasMember(key) && json[key].IsString(), "invalid session control member");
			return {json[key].GetString(), json[key].GetStringLength()};
		};
		if (status != 200) {
			reset = detail::session_reset_reason(status, text("error"),
												 json.HasMember("reason") ? text("reason") : std::string_view{});
			return;
		}
		require(json.MemberCount() == 3 && json.HasMember("version") && json["version"].IsUint64() &&
					json["version"].GetUint64() == 2 && json.HasMember("wait_ms") && json["wait_ms"].IsUint64() &&
					json["wait_ms"].GetUint64() <= 60000,
				"invalid session opening response");
		opened = NamespaceSession{identity(text("session")), std::chrono::milliseconds(json["wait_ms"].GetUint64())};
	}
	void rethrow() const {
		if (error)
			std::rethrow_exception(error);
	}
};
FramesResponseDecoder::FramesResponseDecoder(SessionResponsePurpose purpose, d::Identity session, bool compression,
											 d::FrameSink *sink, d::Limits limits)
	: impl_(std::make_unique<Impl>(purpose, session, compression, sink, std::move(limits))) {}
FramesResponseDecoder::~FramesResponseDecoder() = default;
void FramesResponseDecoder::feed(std::span<const char> bytes) {
	auto &p = *impl_;
	require(!p.complete, "bytes after the session response");
	const auto consumed = http_parser_execute(&p.parser, &p.settings, bytes.data(), bytes.size());
	p.rethrow();
	require(HTTP_PARSER_ERRNO(&p.parser) == HPE_OK || HTTP_PARSER_ERRNO(&p.parser) == HPE_PAUSED,
			"invalid session response HTTP");
	require(consumed == bytes.size(), "bytes after the session response");
}
void FramesResponseDecoder::eof() {
	auto &p = *impl_;
	if (!p.complete) {
		http_parser_execute(&p.parser, &p.settings, nullptr, 0);
		p.rethrow();
	}
	require(p.complete, "session response truncated");
}
bool FramesResponseDecoder::done() const noexcept { return impl_->complete; }
bool FramesResponseDecoder::reusable() const noexcept {
	return impl_->complete && http_should_keep_alive(&impl_->parser);
}
NamespaceSession FramesResponseDecoder::opened() const {
	require(impl_->complete, "session response incomplete");
	if (impl_->reset)
		throw SessionResetError(*impl_->reset);
	require(impl_->opened.has_value(), "not an opening response");
	return *impl_->opened;
}
FramesOutcome FramesResponseDecoder::outcome() const {
	require(impl_->complete, "session response incomplete");
	if (impl_->reset)
		throw SessionResetError(*impl_->reset);
	return impl_->outcome;
}

namespace {
task::task<void> exchange(ClientOptions options, std::string path, std::string body, FramesResponseDecoder &decoder,
						  task::cancel_token token) {
	detail::validate_transport(options);
	require(body.size() <= options.request_bytes, "session request byte bound");
	const auto expires = std::chrono::steady_clock::now() + options.timeout;
	task::deadline deadline(*options.scheduler, expires, token);
	auto wire = "POST " + path + " HTTP/1.1\r\nHost: " + options.hostname + "\r\nAuthorization: Bearer " +
				options.token + "\r\nContent-Type: application/json\r\nConnection: " +
				(options.connections ? "keep-alive" : "close") +
				"\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
	const auto executor = options.scheduler->executor();
	std::exception_ptr failure;
	{
		ConnectionLease connection(options, executor);
		auto &socket = connection.stream();
		bool reusable = false;
		try {
			const auto cancel = deadline.token();
			if (!connection.connected()) {
				co_await transport::await_io<void(asio::error_code)>(
					[&](auto handler) { socket.next_layer().async_connect(options.endpoint, std::move(handler)); },
					cancel);
				connection.tcp_connected();
				const asio::error_code handshake = co_await task::await_asio<void(asio::error_code)>(
					[&](auto handler) {
						socket.async_handshake(asio::ssl::stream_base::client, std::move(handler));
					},
					cancel);
				if (SSL_get_verify_result(socket.native_handle()) != X509_V_OK)
					throw SourceTrustError("delivery TLS verification failed");
				if (handshake)
					throw asio::system_error(handshake);
				connection.mark_connected();
			}
			co_await transport::await_io<void(asio::error_code, std::size_t)>(
				[&](auto handler) {
					asio::async_write(socket, asio::buffer(wire.data(), wire.size()), std::move(handler));
				},
				cancel);
			std::array<char, 4096> input;
			while (!decoder.done()) {
				const auto [error, n] = co_await task::await_asio<void(asio::error_code, std::size_t)>(
					[&](auto handler) { socket.async_read_some(asio::buffer(input), std::move(handler)); }, cancel);
				if (n)
					decoder.feed({input.data(), n});
				if (!decoder.done() && (error || !n)) {
					try {
						decoder.eof();
					} catch (const std::invalid_argument &) {
						throw SourceUnavailableError("delivery transport closed before complete response");
					}
				}
			}
			reusable = decoder.reusable();
		} catch (...) {
			failure = std::current_exception();
		}
		connection.reusable(reusable && !failure);
	}
	if (failure) {
		if (deadline.expired())
			throw SourceUnavailableError("delivery network deadline");
		std::rethrow_exception(failure);
	}
}
} // namespace

task::task<NamespaceSession> open_namespace_session(ClientOptions options, Following following,
													task::cancel_token token) {
	FramesResponseDecoder decoder(SessionResponsePurpose::Open, {}, options.compression, nullptr, options.limits);
	auto body = namespace_session_body(SessionResponsePurpose::Open, following, options.compression);
	co_await exchange(std::move(options), "/v2/distribution/session/open", std::move(body), decoder, std::move(token));
	co_return decoder.opened();
}
task::task<FramesOutcome> next_namespace_session(ClientOptions options, NamespaceSession session, Following following,
												 std::chrono::milliseconds wait, d::FrameSink &sink,
												 task::cancel_token token) {
	require(session.valid() && wait >= std::chrono::milliseconds::zero() && wait <= session.maximum_wait,
			"invalid session next");
	FramesResponseDecoder decoder(SessionResponsePurpose::Next, session.id, options.compression, &sink,
								  options.limits);
	auto body = namespace_session_body(SessionResponsePurpose::Next, following, options.compression, session.id, wait);
	co_await exchange(std::move(options), "/v2/distribution/session/next", std::move(body), decoder, std::move(token));
	co_return decoder.outcome();
}
task::task<void> close_namespace_session(ClientOptions options, NamespaceSession session, task::cancel_token token) {
	require(session.valid(), "invalid session close");
	FramesResponseDecoder decoder(SessionResponsePurpose::Close, session.id, options.compression, nullptr,
								  options.limits);
	auto body = namespace_session_body(SessionResponsePurpose::Close, {}, options.compression, session.id);
	co_await exchange(std::move(options), "/v2/distribution/session/close", std::move(body), decoder, std::move(token));
	try {
		decoder.outcome();
	} catch (const SessionResetError &error) {
		if (error.reason() != SessionResetReason::Expired)
			throw;
	}
}
} // namespace detent::delivery
