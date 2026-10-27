#include "detent_http/http_client.hh"
#include <array>
#include <compressor_deflate.h>
#include <compressor_zstd.h>
#include <http_client_posix.h>
#include <utility>

namespace detent_http {
namespace client = http::client;
namespace {
std::string_view trim(std::string_view value) {
	while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
		value.remove_prefix(1);
	while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
		value.remove_suffix(1);
	return value;
}
HttpResponse interpret(client::BufferedResponse response) {
	HttpResponse result;
	result.status = static_cast<int>(response.head.status);
	std::string_view coding;
	bool has_coding = false, invalid_coding = false;
	for (const auto &header : response.head.headers) {
		const auto value = trim(header.value);
		if (header.name == "etag")
			result.etag = value;
		else if (header.name == "content-type")
			result.is_patch =
			    http::iequal(trim(value.substr(0, value.find(';'))), "application/merge-patch+json");
		else if (header.name == "content-encoding") {
			invalid_coding = has_coding || value.empty();
			has_coding = true;
			coding = value;
		}
	}
	if (result.status == 204 || result.status == 304)
		return result;
	if (invalid_coding)
		throw std::runtime_error("detent_http: invalid or chained Content-Encoding");
	const auto maximum = static_cast<std::size_t>(client::Limits{}.body_bytes);
	if (http::iequal(coding, "zstd"))
		result.body = decompress_zstd(response.body, maximum);
	else if (http::iequal(coding, "gzip"))
		result.body = decompress_deflate(response.body, maximum, true);
	else if (coding.empty() || http::iequal(coding, "identity"))
		result.body = response.body;
	else
		throw std::runtime_error("detent_http: unsupported Content-Encoding");
	return result;
}
std::unique_ptr<client::PosixConnection> connect(std::string_view host, std::uint16_t port,
                                                 client::Deadline deadline) {
	std::array<client::Endpoint, client::default_resolved_endpoints> endpoints;
	const auto count = client::resolve(host, port, endpoints, deadline);
	return std::make_unique<client::PosixConnection>(std::span(endpoints).first(count), deadline);
}
client::ResponsePolicy policy() {
	client::ResponsePolicy result;
	result.equal_content_lengths = true;
	result.eof_delimited = true;
	return result;
}
HttpResponse one_shot(std::string_view method, const std::string &host, std::uint16_t port,
                      const std::string &path, std::string_view body, std::chrono::milliseconds timeout,
                      const std::optional<std::string> &etag = {}) {
	const client::Deadline deadline(timeout);
	std::array<client::RequestField, 2> fields;
	std::size_t count = 0;
	if (method == "GET") {
		fields[count++] = {"Accept-Encoding", "zstd, gzip"};
		if (etag)
			fields[count++] = {"If-None-Match", *etag};
	} else
		fields[count++] = {"Content-Type",
		                   method == "PATCH" ? "application/merge-patch+json" : "application/json"};
	client::PreparedRequest request({method, host, path, std::span(fields).first(count), body, true});
	auto connection = connect(host, port, deadline);
	return interpret(client::exchange(*connection, std::move(request), deadline, {}, policy()));
}
} // namespace
struct HttpConnection::Impl {
	std::string host;
	std::uint16_t port;
	std::chrono::milliseconds timeout, maximum_age;
	std::chrono::steady_clock::time_point connected_at{};
	std::unique_ptr<client::PosixConnection> connection;
	Impl(std::string hostname, std::uint16_t number, std::chrono::milliseconds wait,
	     std::chrono::milliseconds age)
	    : host(std::move(hostname)), port(number), timeout(wait), maximum_age(age) {
		(void)client::Deadline(timeout);
		if (maximum_age.count() < 0)
			throw std::invalid_argument("detent_http: negative connection age");
	}
	void ensure_connected(client::Deadline deadline) {
		if (connection && maximum_age.count() &&
		    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
		                                                          connected_at) >= maximum_age)
			connection.reset();
		if (!connection) {
			connection = connect(host, port, deadline);
			connected_at = std::chrono::steady_clock::now();
		}
	}
	HttpResponse get(const std::string &path, const std::optional<std::string> &etag,
	                 std::optional<std::uint64_t> version) {
		const client::Deadline deadline(timeout);
		std::array<client::RequestField, 3> fields;
		std::size_t count = 0;
		fields[count++] = {"Accept-Encoding", "zstd, gzip"};
		if (etag)
			fields[count++] = {"If-None-Match", *etag};
		const std::string base = version ? std::to_string(*version) : std::string{};
		if (version)
			fields[count++] = {"X-Detent-Base-Version", base};
		bool retry = connection != nullptr;
		for (;;) {
			client::PreparedRequest request({"GET", host, path, std::span(fields).first(count)});
			try {
				ensure_connected(deadline);
				auto response = client::exchange(*connection, std::move(request), deadline, {}, policy());
				if (!response.reusable)
					connection.reset();
				return interpret(std::move(response));
			} catch (const client::Error &error) {
				connection.reset();
				if (!retry || (error.kind() != client::ErrorKind::Transport &&
				               error.kind() != client::ErrorKind::Connect))
					throw;
				retry = false;
				(void)deadline.remaining();
			} catch (...) {
				connection.reset();
				throw;
			}
		}
	}
};
HttpConnection::HttpConnection(std::string host, std::uint16_t port, std::chrono::milliseconds timeout,
                               std::chrono::milliseconds maximum_age)
    : impl_(std::make_unique<Impl>(std::move(host), port, timeout, maximum_age)) {}
HttpConnection::~HttpConnection() = default;
HttpResponse HttpConnection::get(const std::string &path, const std::optional<std::string> &etag,
                                 std::optional<std::uint64_t> version) {
	return impl_->get(path, etag, version);
}
HttpResponse http_get(const std::string &host, std::uint16_t port, const std::string &path,
                      std::chrono::milliseconds timeout, const std::optional<std::string> &etag) {
	return one_shot("GET", host, port, path, {}, timeout, etag);
}
HttpResponse http_put(const std::string &host, std::uint16_t port, const std::string &path, std::string body,
                      std::chrono::milliseconds timeout) {
	return one_shot("PUT", host, port, path, body, timeout);
}
HttpResponse http_patch(const std::string &host, std::uint16_t port, const std::string &path,
                        std::string body, std::chrono::milliseconds timeout) {
	return one_shot("PATCH", host, port, path, body, timeout);
}
} // namespace detent_http
