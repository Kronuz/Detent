#include "detent_delivery/client.hh"
#include "detent/validation.hh"
#include "detent_distribution/memory_resource.hh"
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <exception>
#include <variant>
namespace detent::delivery {
namespace d = distribution;
namespace {
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
std::pmr::string request(const ClientOptions &options, std::pmr::memory_resource &resource) {
	std::pmr::string body(&resource);
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
	body += '}';
	require(body.size() <= options.request_bytes, "delivery request byte bound");
	std::pmr::string wire(&resource);
	wire += "POST /v2/distribution HTTP/1.1\r\nHost: ";
	wire += options.hostname;
	wire += "\r\nAuthorization: Bearer ";
	wire += options.token;
	wire += "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ";
	wire += std::to_string(body.size());
	wire += "\r\n\r\n";
	wire += body;
	return wire;
}
asio::awaitable<ReceivedUpdate> receive_one(ClientOptions options,
											std::chrono::steady_clock::time_point expires) {
	const auto executor = co_await asio::this_coro::executor;
	options.limits = d::detail::admitted_limits(std::move(options.limits));
	auto account = options.limits.storage_account;
	d::LedgerMemoryResource resource(account);
	std::vector<std::string_view, d::StorageAllocator<std::string_view>> names{
		d::StorageAllocator<std::string_view>(account)};
	names.reserve(options.namespaces.size());
	for (const auto &name : options.namespaces)
		names.push_back(name);
	auto decoder = d::make_storage_owner<ResponseDecoder>(account, std::malloc, std::free, options.configured,
														  options.base, d::NamespaceSelection(names),
														  options.compression, options.limits);
	auto input = d::make_owned_buffer(account, 4096);
	input->resize(4096);
	auto wire = request(options, resource);
	asio::ssl::stream<asio::ip::tcp::socket> socket(executor, *options.tls_context);
	transport::configure_publisher_client(socket.native_handle(), options.hostname);
	std::exception_ptr failure;
	std::optional<ReceivedUpdate> result;
	try {
		co_await socket.next_layer().async_connect(options.endpoint, asio::use_awaitable);
		co_await socket.async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);
		co_await asio::async_write(socket, asio::buffer(wire.data(), wire.size()), asio::use_awaitable);
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
					decoder->eof();
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
		result.emplace(decoder->take());
	} catch (...) {
		failure = std::current_exception();
	}
	// The original await has settled before these buffers or the TLS stream
	// can retire. Cleanup remains scheduled even after terminal cancellation.
	// Cleanup uses only posted CPU work. Preserve cancellation's latched state
	// while suppressing automatic throws at those cleanup suspension points.
	co_await asio::this_coro::throw_if_cancelled(false);
	asio::error_code ignored;
	socket.next_layer().close(ignored);
	for (;;) {
		d::WorkBudget work;
		if (decoder->discard(work))
			break;
		co_await asio::post(executor, asio::use_awaitable);
	}
	const auto cancellation = co_await asio::this_coro::cancellation_state;
	if (result && (cancellation.cancelled() != asio::cancellation_type::none ||
				   std::chrono::steady_clock::now() >= expires)) {
		for (;;) {
			d::WorkBudget work;
			const bool drained =
				result->snapshot ? result->snapshot->discard(work) : result->delta->discard(work);
			if (drained)
				break;
			co_await asio::post(executor, asio::use_awaitable);
		}
		result.reset();
		if (!failure)
			failure = std::make_exception_ptr(asio::system_error(asio::error::operation_aborted));
	}
	if (failure)
		std::rethrow_exception(failure);
	co_return std::move(*result);
}
struct Outcome {
	std::variant<ReceivedUpdate, std::exception_ptr> value;
};
asio::awaitable<Outcome> outcome(ClientOptions options, std::chrono::steady_clock::time_point expires) {
	try {
		co_return Outcome{co_await receive_one(std::move(options), expires)};
	} catch (...) {
		co_return Outcome{std::current_exception()};
	}
}
} // namespace
asio::awaitable<ReceivedUpdate> receive(ClientOptions options) {
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
	const auto executor = co_await asio::this_coro::executor;
	const auto expires = std::chrono::steady_clock::now() + options.timeout;
	asio::steady_timer deadline(executor, expires);
	asio::error_code timer_error;
	using namespace asio::experimental::awaitable_operators;
	auto result = co_await (outcome(std::move(options), expires) ||
							deadline.async_wait(asio::redirect_error(asio::use_awaitable, timer_error)));
	if (result.index() != 0)
		throw std::runtime_error("delivery network deadline");
	auto received = std::move(std::get<0>(result).value);
	if (auto error = std::get_if<std::exception_ptr>(&received))
		std::rethrow_exception(*error);
	co_return std::move(std::get<ReceivedUpdate>(received));
}
} // namespace detent::delivery
