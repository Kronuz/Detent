#include "detent_delivery/client.hh"
#include "client_probe_internal.hh"
#include "connection_key.hh"
#include "detent/validation.hh"
#include "detent_distribution/memory_resource.hh"
#include <asio.hpp>
#include <cstring>
#include <exception>
#include <rapidjson/document.h>
#include <sha256.h>
#include <variant>
namespace detent::delivery {
namespace d = distribution;
namespace client = http::client;
namespace detail {
enum class ClientBodyMode { Semantic, Probe, Reference };
}
namespace {
using BodyMode = detail::ClientBodyMode;
struct OpenedSession {
	d::Identity id;
	bool snapshot;
	std::chrono::milliseconds wait;
};
struct ValidatedControl {
	ReceivedControl wire;
	std::optional<OpenedSession> opened;
	std::optional<SessionResetReason> reason;
};
using TransportResponse = std::variant<ReceivedUpdate, ValidatedControl, detail::ProbeOffer>;
SessionResetReason reset_reason(const ReceivedControl &, std::pmr::memory_resource &);
struct RequestAction {
	SessionResponsePurpose purpose;
	d::Identity session{};
	std::uint64_t after = 0;
	std::optional<d::Cursor> published;
	std::chrono::milliseconds wait{0};
	bool force_snapshot = false, snapshot_allowed = false;
};
ValidatedControl validate_control(ReceivedControl, const ClientOptions &, std::optional<RequestAction>,
                                  std::pmr::memory_resource &);
void require(bool condition, const char *message) {
	if (!condition)
		throw std::invalid_argument(message);
}
void hex(std::pmr::string &out, std::span<const char> value) {
	for (unsigned char byte : value) {
		out += "0123456789abcdef"[byte >> 4];
		out += "0123456789abcdef"[byte & 15];
	}
}
void cursor(std::pmr::string &out, const d::Cursor &value, bool revision) {
	out += "\"cluster\":\"";
	hex(out, value.cluster);
	out += "\",\"group\":\"";
	hex(out, value.group);
	out += "\",\"epoch\":\"";
	hex(out, value.restore_epoch);
	out += "\",\"scope\":\"";
	hex(out, value.scope);
	out += '"';
	if (revision) {
		out += ",\"revision\":";
		out += std::to_string(value.revision);
	}
}
client::PreparedRequest request(const ClientOptions &options, allocators::AllocationContext context,
                                std::optional<RequestAction> action) {
	std::pmr::string body(context.resource());
	if (!action || action->purpose == SessionResponsePurpose::Open) {
		body += '{';
		cursor(body, options.configured, false);
		body += ",\"profile\":1,\"coding\":\"";
		body += options.compression ? "zstd" : "raw";
		body += "\",\"namespaces\":[";
		bool first = true;
		std::string_view previous;
		for (const auto &name : options.namespaces) {
			require(!name.empty() && name.size() <= 198 &&
			            std::all_of(name.begin(), name.end(), detent::detail::is_name_char) &&
			            (previous.empty() || previous < name),
			        "delivery request scope is not canonical");
			if (!first)
				body += ',';
			first = false;
			body += '"';
			body += name;
			body += '"';
			previous = name;
			require(body.size() <= options.request_bytes, "delivery request byte bound");
		}
		body += ']';
		if (options.base) {
			body += ",\"base\":{";
			cursor(body, *options.base, true);
			body += '}';
		}
		if (action) {
			body += ",\"version\":1,\"force_snapshot\":";
			body += action->force_snapshot ? "true" : "false";
		}
		body += '}';
	} else {
		body += "{\"version\":1,\"session\":\"";
		hex(body, action->session);
		body += '"';
		if (action->purpose == SessionResponsePurpose::Next) {
			body += ",\"after\":";
			body += std::to_string(action->after);
			body += ",\"wait_ms\":";
			body += std::to_string(action->wait.count());
			if (action->published) {
				body += ",\"published\":{";
				cursor(body, *action->published, true);
				body += '}';
			}
		}
		body += '}';
	}
	require(body.size() <= options.request_bytes, "delivery request byte bound");
	std::pmr::string target(context.resource());
	target = "/v2/distribution";
	if (action) {
		target += "/session/";
		target += action->purpose == SessionResponsePurpose::Open   ? "open"
		          : action->purpose == SessionResponsePurpose::Next ? "next"
		                                                            : "close";
	}
	std::pmr::string authorization("Bearer ", context.resource());
	authorization += options.token;
	const std::array fields{client::RequestField{"Authorization", authorization},
	                        client::RequestField{"Content-Type", "application/json"}};
	client::Limits limits;
	limits.body_bytes = options.request_bytes;
	return client::PreparedRequest({"POST", options.hostname, target, fields, body, !options.connections},
	                               limits, context);
}

asio::awaitable<TransportResponse> receive_one(ClientOptions options,
                                               std::chrono::steady_clock::time_point expires,
                                               std::optional<RequestAction> action, BodyMode mode) {
	const auto executor = co_await asio::this_coro::executor;
	options.limits = d::detail::admitted_limits(std::move(options.limits));
	auto account = options.limits.storage_account;
	auto resource = allocators::make_charged_resource(account, options.limits.storage_allocate,
	                                                  options.limits.storage_release);
	const allocators::AllocationContext context(resource);
	std::vector<std::string_view, d::StorageAllocator<std::string_view>> names{
	    d::StorageAllocator<std::string_view>(account)};
	names.reserve(options.namespaces.size());
	for (const auto &name : options.namespaces)
		names.push_back(name);
	std::optional<SessionResponseBinding> binding;
	if (action)
		binding =
		    SessionResponseBinding{action->purpose, action->session,
			                       action->purpose == SessionResponsePurpose::Next ? action->after + 1 : 0,
			                       action->snapshot_allowed};
	auto decoder = d::make_storage_owner<ResponseDecoder>(account, std::malloc, std::free, options.configured,
	                                                      options.base, d::NamespaceSelection(names),
	                                                      options.compression, options.limits, binding);
	if (mode != BodyMode::Semantic)
		detail::ProbeDecoderAccess::prepare(*decoder, mode == BodyMode::Reference);
	auto input = d::make_owned_buffer(account, 4096);
	input->resize(4096);
	auto wire = request(options, context, action);
	client::TlsLease connection(connection_key(options), executor, options.connections, context);
	auto &socket = connection.stream();
	std::exception_ptr failure;
	std::optional<TransportResponse> result;
	bool reusable = true;
	try {
		co_await client::connect(connection);
		co_await client::write(socket, std::move(wire));
		while (!decoder->done()) {
			if (decoder->needs_input()) {
				asio::error_code error;
				const auto n = co_await socket.async_read_some(
				    asio::buffer(input->mutable_bytes().data(), input->size()),
				    asio::redirect_error(asio::use_awaitable, error));
				auto bytes = std::span<const char>(input->data(), n);
				while (!bytes.empty()) {
					d::WorkBudget work;
					decoder->feed(bytes, work);
					if (!bytes.empty())
						co_await asio::post(executor, asio::use_awaitable);
				}
				if (error || !n) {
					reusable = false;
					try {
						decoder->eof();
					} catch (const std::invalid_argument &) {
						throw SourceUnavailableError("delivery transport closed before complete response");
					}
					if (error && error != asio::error::eof && error != asio::ssl::error::stream_truncated)
						throw asio::system_error(error);
				}
			} else {
				d::WorkBudget work;
				decoder->step(work);
				if (!decoder->done())
					co_await asio::post(executor, asio::use_awaitable);
			}
		}
		reusable = reusable && decoder->reusable();
		if (decoder->is_control())
			result.emplace(validate_control(decoder->take_control(), options, action, *resource));
		else if (mode == BodyMode::Semantic)
			result.emplace(decoder->take());
		else
			result.emplace(detail::ProbeDecoderAccess::take(*decoder));
	} catch (...) {
		failure = std::current_exception();
	}
	// The original await has settled before these buffers or the TLS stream
	// can retire. Cleanup remains scheduled even after terminal cancellation.
	// Cleanup uses only posted CPU work. Preserve cancellation's latched state
	// while suppressing automatic throws at those cleanup suspension points.
	co_await asio::this_coro::throw_if_cancelled(false);
	for (;;) {
		d::WorkBudget work;
		if (decoder->discard(work))
			break;
		co_await asio::post(executor, asio::use_awaitable);
	}
	const auto cancellation = co_await asio::this_coro::cancellation_state;
	if (result && (cancellation.cancelled() != asio::cancellation_type::none ||
	               std::chrono::steady_clock::now() >= expires)) {
		if (auto update = std::get_if<ReceivedUpdate>(&*result)) {
			for (;;) {
				d::WorkBudget work;
				const bool drained =
				    update->snapshot ? update->snapshot->discard(work) : update->delta->discard(work);
				if (drained)
					break;
				co_await asio::post(executor, asio::use_awaitable);
			}
		} else if (const auto *value = std::get_if<ValidatedControl>(&*result)) {
			auto control = value->wire.body;
			auto bytes = control ? control->storage_capacity() : 0;
			while (bytes) {
				bytes -= std::min<std::size_t>(bytes, 8192);
				co_await asio::post(executor, asio::use_awaitable);
			}
		}
		result.reset();
		if (!failure)
			failure = std::make_exception_ptr(asio::system_error(asio::error::operation_aborted));
	}
	if (failure)
		std::rethrow_exception(failure);
	connection.reusable(reusable);
	co_return std::move(*result);
}
void validate(const ClientOptions &options) {
	require(options.tls_context &&
	            (SSL_CTX_get_verify_mode(options.tls_context->native_handle()) & SSL_VERIFY_PEER) &&
	            options.endpoint.port() && !options.hostname.empty() && options.hostname.size() <= 253 &&
	            options.hostname.find_first_of("\r\n") == std::string::npos &&
	            options.hostname.find('\0') == std::string::npos && options.token.size() >= 32 &&
	            options.token.size() <= 128 &&
	            std::all_of(options.token.begin(), options.token.end(),
	                        [](unsigned char c) { return c >= 32 && c != 127; }) &&
	            options.timeout > std::chrono::milliseconds::zero() &&
	            options.timeout <= std::chrono::hours(24) && options.request_bytes &&
	            options.request_bytes <= 1024 * 1024 &&
	            options.namespaces.size() <= options.limits.namespaces,
	        "invalid delivery client options");
}
asio::awaitable<TransportResponse> perform(ClientOptions options, std::optional<RequestAction> action = {},
                                           BodyMode mode = BodyMode::Semantic) {
	validate(options);
	require(mode == BodyMode::Semantic || (action && action->purpose == SessionResponsePurpose::Next),
	        "probe exchange requires session Next");
	const client::Deadline deadline(options.timeout);
	try {
		co_return co_await client::before(deadline,
		                                  receive_one(std::move(options), deadline.expires(), action, mode));
	} catch (const client::Error &error) {
		if (error.kind() == client::ErrorKind::Trust)
			throw SourceTrustError(error.what());
		if (error.kind() == client::ErrorKind::Cancelled)
			throw asio::system_error(asio::error::operation_aborted);
		if (error.kind() == client::ErrorKind::Pressure &&
			error.cause() != std::errc::resource_unavailable_try_again)
			throw d::StoragePressure(error.what());
		if ((error.kind() == client::ErrorKind::Transport || error.kind() == client::ErrorKind::Connect) && error.cause())
			throw asio::system_error(error.cause(), error.what());
		if (error.kind() == client::ErrorKind::Options || error.kind() == client::ErrorKind::Framing ||
		    error.kind() == client::ErrorKind::Limit)
			throw std::invalid_argument(error.what());
		throw SourceUnavailableError(error.what());
	}
}
} // namespace
asio::awaitable<ReceivedUpdate> receive(ClientOptions options) {
	options.limits = d::detail::admitted_limits(std::move(options.limits));
	auto response = co_await perform(std::move(options));
	if (auto *control = std::get_if<ValidatedControl>(&response))
		throw SessionResetError(control->reason.value());
	co_return std::move(std::get<ReceivedUpdate>(response));
}

struct SessionHandle::State {
	d::LedgerMemoryResource resource;
	d::ScopeIdentity namespace_digest{};
	std::size_t namespace_count;
	d::Cursor configured;
	asio::ip::tcp::endpoint endpoint;
	std::pmr::string hostname;
	std::shared_ptr<asio::ssl::context> tls_context;
	std::optional<d::Cursor> base;
	d::Identity id{};
	std::optional<PublicationFeedback> offered;
	std::optional<BodyMode> body_mode;
	std::uint64_t after = 0;
	std::chrono::milliseconds wait{0};
	bool compression, snapshot = false, alive = true, busy = false, closed = false;
	explicit State(const ClientOptions &options)
	    : resource(options.limits.storage_account), namespace_count(options.namespaces.size()),
	      configured(options.configured), endpoint(options.endpoint), hostname(options.hostname, &resource),
	      tls_context(options.tls_context), base(options.base), compression(options.compression) {}
};
namespace {
struct JsonAllocator {
	static constexpr bool kNeedFree = true;
	struct alignas(std::max_align_t) Header {
		std::pmr::memory_resource *resource;
		std::size_t bytes;
	};
	std::pmr::memory_resource *resource = nullptr;
	void *Malloc(std::size_t bytes) {
		if (!bytes)
			return nullptr;
		require(resource != nullptr, "missing session JSON storage account");
		auto *header = static_cast<Header *>(resource->allocate(sizeof(Header) + bytes, alignof(Header)));
		*header = {resource, sizeof(Header) + bytes};
		return header + 1;
	}
	static void Free(void *pointer) noexcept {
		if (!pointer)
			return;
		auto *header = static_cast<Header *>(pointer) - 1;
		header->resource->deallocate(header, header->bytes, alignof(Header));
	}
	void *Realloc(void *pointer, std::size_t original, std::size_t bytes) {
		auto *replacement = Malloc(bytes);
		if (pointer && replacement)
			std::memcpy(replacement, pointer, std::min(original, bytes));
		Free(pointer);
		return replacement;
	}
};
using Json = rapidjson::GenericDocument<rapidjson::UTF8<>, JsonAllocator, JsonAllocator>;
using Value = Json::ValueType;
void parse(Json &json, const ReceivedControl &control) {
	require(control.body && control.body->size() && control.body->size() <= 4096,
	        "missing session control body");
	json.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(control.body->data(),
	                                                                                   control.body->size());
	require(!json.HasParseError() && json.IsObject(), "invalid session control JSON");
	for (auto a = json.MemberBegin(); a != json.MemberEnd(); ++a)
		for (auto b = json.MemberBegin(); b != a; ++b)
			require(a->name != b->name, "duplicate session control member");
}
const Value &member(const Json &json, const char *key) {
	require(json.HasMember(key), "missing session control member");
	return json[key];
}
std::string_view string(const Value &value) {
	require(value.IsString(), "invalid session control string");
	return {value.GetString(), value.GetStringLength()};
}
d::Identity identity(const Value &value) {
	const auto text = string(value);
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
SessionResetReason reset_reason(const ReceivedControl &control, std::pmr::memory_resource &resource) {
	JsonAllocator allocator{&resource};
	Json json(&allocator, 1024, &allocator);
	parse(json, control);
	const auto error = string(member(json, "error"));
	if (control.status == 401)
		return SessionResetReason::Unauthorized;
	if (control.status == 403)
		return SessionResetReason::Forbidden;
	if (control.status == 410 && error == "session_expired")
		return SessionResetReason::Expired;
	if (control.status == 409) {
		if (error == "snapshot_required") {
			if (json.HasMember("reason") && string(json["reason"]) == "representation_limit")
				return SessionResetReason::RepresentationLimit;
			return SessionResetReason::HistoryLost;
		}
		if (error == "identity_changed")
			return SessionResetReason::IdentityChanged;
		if (error == "session_out_of_sync")
			return SessionResetReason::OutOfSync;
		if (error == "session_busy")
			return SessionResetReason::Busy;
	}
	if (control.status == 503 && error == "source_not_leader")
		return SessionResetReason::NotLeader;
	if (control.status == 503 && (error == "outcome_unknown" || error == "stopping" ||
	                              error == "distribution_unavailable" || error == "authorization_timeout"))
		return SessionResetReason::Unavailable;
	if (control.status == 503 && error == "source_behind")
		return SessionResetReason::SourceBehind;
	if (control.status == 503 || control.status == 429)
		return SessionResetReason::AdmissionFull;
	if (control.status == 400)
		return SessionResetReason::InvalidRequest;
	throw std::invalid_argument("unknown session control response");
}
ValidatedControl validate_control(ReceivedControl control, const ClientOptions &options,
                                  std::optional<RequestAction> action, std::pmr::memory_resource &resource) {
	ValidatedControl result{std::move(control), {}, {}};
	if (result.wire.status == 204)
		return result;
	if (result.wire.status != 200) {
		result.reason = reset_reason(result.wire, resource);
		return result;
	}
	require(action && action->purpose == SessionResponsePurpose::Open, "unexpected opening control");
	JsonAllocator allocator{&resource};
	Json json(&allocator, 1024, &allocator);
	parse(json, result.wire);
	require(json.MemberCount() == 5 && member(json, "version").IsUint() && json["version"].GetUint() == 1 &&
	            member(json, "window").IsUint64() && json["window"].GetUint64() == 0 &&
	            member(json, "wait_ms").IsUint64() && json["wait_ms"].GetUint64() <= 60000,
	        "invalid session opening response");
	const auto id = identity(member(json, "session"));
	const auto mode = string(member(json, "mode"));
	require(mode == "snapshot" || (mode == "resume" && options.base && !action->force_snapshot),
	        "invalid session opening mode");
	result.opened =
	    OpenedSession{id, mode == "snapshot", std::chrono::milliseconds(json["wait_ms"].GetUint64())};
	return result;
}
struct SessionOperation {
	std::shared_ptr<SessionHandle::State> state;
	explicit SessionOperation(std::shared_ptr<SessionHandle::State> value) : state(std::move(value)) {
		require(state && !state->busy, "session operation already pending");
		state->busy = true;
	}
	~SessionOperation() { state->busy = false; }
};
asio::awaitable<d::ScopeIdentity> namespace_digest(const std::vector<std::string> &names) {
	class SHA256 hash;
	constexpr std::string_view domain = "detent-session-namespaces-v1";
	hash.add(domain.data(), domain.size());
	auto number = [&](std::uint64_t value) {
		std::array<unsigned char, 8> bytes{};
		for (unsigned i = 0; i < 8; ++i)
			bytes[7 - i] = static_cast<unsigned char>(value >> (i * 8));
		hash.add(bytes.data(), bytes.size());
	};
	number(names.size());
	std::string_view previous;
	std::size_t bytes = 0;
	for (const auto &name : names) {
		require(!name.empty() && name.size() <= 198 && (previous.empty() || previous < name) &&
		            std::all_of(name.begin(), name.end(), detent::detail::is_name_char),
		        "session namespace selection is not canonical");
		number(name.size());
		hash.add(name.data(), name.size());
		previous = name;
		bytes += name.size() + 8;
		if (bytes >= 8192) {
			co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
			bytes = 0;
		}
	}
	std::array<unsigned char, 32> digest{};
	hash.getHash(digest.data());
	d::ScopeIdentity result{};
	std::copy_n(reinterpret_cast<const char *>(digest.data()), result.size(), result.begin());
	co_return result;
}
asio::awaitable<void> same_configuration(const ClientOptions &options, const SessionHandle::State &state) {
	require(options.endpoint == state.endpoint &&
	            std::string_view(options.hostname) == std::string_view(state.hostname) &&
	            options.tls_context == state.tls_context,
	        "session source destination changed");
	require(options.configured == state.configured && options.compression == state.compression &&
	            options.namespaces.size() == state.namespace_count,
	        "session configuration changed");
	require((co_await namespace_digest(options.namespaces)) == state.namespace_digest,
	        "session namespace selection changed");
}
} // namespace
SessionResetError::SessionResetError(SessionResetReason reason)
	: std::runtime_error([&] {
		  switch (reason) {
		  case SessionResetReason::HistoryLost: return "distribution session reset: history_lost";
		  case SessionResetReason::RepresentationLimit: return "distribution session reset: representation_limit";
		  case SessionResetReason::Expired: return "distribution session reset: expired";
		  case SessionResetReason::IdentityChanged: return "distribution session reset: identity_changed";
		  case SessionResetReason::OutOfSync: return "distribution session reset: out_of_sync";
		  case SessionResetReason::Busy: return "distribution session reset: busy";
		  case SessionResetReason::AdmissionFull: return "distribution session reset: admission_full";
		  case SessionResetReason::Forbidden: return "distribution session reset: forbidden";
		  case SessionResetReason::Unauthorized: return "distribution session reset: unauthorized";
		  case SessionResetReason::InvalidRequest: return "distribution session reset: invalid_request";
		  case SessionResetReason::SourceBehind: return "distribution session reset: source_behind";
		  case SessionResetReason::NotLeader: return "distribution session reset: not_leader";
		  case SessionResetReason::Unavailable: return "distribution session reset: unavailable";
		  }
		  return "distribution session reset: unknown";
	  }()), reason_(reason) {}
bool SessionHandle::valid() const noexcept { return state_ && state_->alive && !state_->closed; }
d::Identity SessionHandle::identity() const noexcept { return state_ ? state_->id : d::Identity{}; }
std::chrono::milliseconds SessionHandle::maximum_wait() const noexcept {
	return state_ ? state_->wait : std::chrono::milliseconds::zero();
}
asio::awaitable<SessionHandle> open_session(ClientOptions options, bool force_snapshot) {
	validate(options);
	options.limits = d::detail::admitted_limits(std::move(options.limits));
	auto state = std::allocate_shared<SessionHandle::State>(
	    d::StorageAllocator<SessionHandle::State>(options.limits.storage_account), options);
	state->namespace_digest = co_await namespace_digest(options.namespaces);
	RequestAction action{SessionResponsePurpose::Open};
	action.force_snapshot = force_snapshot;
	auto response = co_await perform(std::move(options), action);
	const auto &control = std::get<ValidatedControl>(response);
	if (control.wire.status != 200)
		throw SessionResetError(control.reason.value());
	const auto &opened = control.opened.value();
	state->id = opened.id;
	state->snapshot = opened.snapshot;
	state->wait = opened.wait;
	SessionHandle result;
	result.state_ = std::move(state);
	co_return result;
}
namespace {
using NextEvent = std::variant<SessionOffer, detail::ProbeOffer, SessionIdle, SessionReset>;
asio::awaitable<NextEvent> next_event(ClientOptions options, std::shared_ptr<SessionHandle::State> state,
                                      std::optional<PublicationFeedback> feedback,
                                      std::chrono::milliseconds wait, BodyMode mode) {
	require(state && state->alive && !state->closed, "session is not live");
	SessionOperation operation(state);
	co_await same_configuration(options, *state);
	require(!state->body_mode || *state->body_mode == mode, "session consumption mode changed");
	require(wait >= std::chrono::milliseconds::zero() && wait <= state->wait,
	        "session wait exceeds negotiated bound");
	require(state->after < std::numeric_limits<std::uint64_t>::max(), "session window exhausted");
	if (state->offered) {
		require(feedback && feedback->window == state->offered->window &&
		            feedback->cursor == state->offered->cursor,
		        "session requires exact publication feedback");
		state->after = feedback->window;
		state->base = feedback->cursor;
		state->offered.reset();
		state->snapshot = false;
	} else if (feedback) {
		require(feedback->window == state->after && state->base && feedback->cursor == *state->base,
		        "session publication feedback does not match accepted window");
	}
	state->body_mode = mode;
	options.base = state->base;
	RequestAction action{SessionResponsePurpose::Next,
	                     state->id,
	                     state->after,
	                     state->after ? state->base : std::nullopt,
	                     wait,
	                     false,
	                     state->snapshot};
	try {
		auto response = co_await perform(std::move(options), action, mode);
		if (auto *update = std::get_if<ReceivedUpdate>(&response)) {
			const auto window = state->after + 1;
			state->offered = PublicationFeedback{window, update->target};
			co_return SessionOffer{window, std::move(*update)};
		}
		if (auto *offer = std::get_if<detail::ProbeOffer>(&response)) {
			require(offer->window == state->after + 1, "probe response window mismatch");
			state->offered = PublicationFeedback{offer->window, offer->target};
			co_return std::move(*offer);
		}
		const auto &control = std::get<ValidatedControl>(response);
		if (control.wire.status == 204)
			co_return SessionIdle{};
		auto reason = control.reason.value();
		if (reason != SessionResetReason::Busy && reason != SessionResetReason::AdmissionFull &&
		    reason != SessionResetReason::SourceBehind)
			state->alive = false;
		co_return SessionReset{reason};
	} catch (...) {
		state->alive = false;
		throw;
	}
}
} // namespace
asio::awaitable<SessionEvent> next_session(ClientOptions options, SessionHandle &handle,
                                           std::optional<PublicationFeedback> feedback,
                                           std::chrono::milliseconds wait) {
	auto event = co_await next_event(std::move(options), handle.state_, feedback, wait, BodyMode::Semantic);
	if (auto *offer = std::get_if<SessionOffer>(&event))
		co_return std::move(*offer);
	if (std::holds_alternative<SessionIdle>(event))
		co_return SessionIdle{};
	co_return std::get<SessionReset>(event);
}
asio::awaitable<detail::ProbeEvent> detail::ProbeAccess::next(ClientOptions options, SessionHandle &handle,
                                                              std::optional<PublicationFeedback> feedback,
                                                              std::chrono::milliseconds wait) {
	auto event = co_await next_event(std::move(options), handle.state_, feedback, wait, BodyMode::Probe);
	if (auto *offer = std::get_if<ProbeOffer>(&event))
		co_return std::move(*offer);
	if (std::holds_alternative<SessionIdle>(event))
		co_return SessionIdle{};
	co_return std::get<SessionReset>(event);
}
asio::awaitable<detail::ProbeEvent>
detail::ProbeAccess::reference(ClientOptions options, SessionHandle &handle,
                               std::optional<PublicationFeedback> feedback, std::chrono::milliseconds wait) {
	auto event = co_await next_event(std::move(options), handle.state_, feedback, wait, BodyMode::Reference);
	if (auto *offer = std::get_if<ProbeOffer>(&event))
		co_return std::move(*offer);
	if (std::holds_alternative<SessionIdle>(event))
		co_return SessionIdle{};
	co_return std::get<SessionReset>(event);
}
asio::awaitable<void> close_session(ClientOptions options, SessionHandle &handle) {
	auto state = handle.state_;
	if (!state || state->closed)
		co_return;
	SessionOperation operation(state);
	co_await same_configuration(options, *state);
	state->alive = false;
	auto response =
	    co_await perform(std::move(options), RequestAction{SessionResponsePurpose::Close, state->id});
	const auto &control = std::get<ValidatedControl>(response);
	if (control.wire.status != 204 && control.wire.status != 410)
		throw SessionResetError(control.reason.value());
	state->closed = true;
}
} // namespace detent::delivery
