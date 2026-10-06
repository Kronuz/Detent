#include "service.hh"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace detent::authority;
namespace {
struct Writer : http::ResponseWriter {
	int code = 0;
	std::string body;
	void status(int c) override { code = c; }
	void set_header(std::string_view, std::string_view) override {}
	void write(std::string_view s) override { body += s; }
	void end() override {}
	void set_close() override {}
};
struct DeferredWriter : Writer {
	std::shared_ptr<http::detail::DeferredExchange> exchange;
	std::weak_ptr<void> admission;
	std::unique_ptr<http::DeferredReply> defer(http::DeferredOptions options) override {
		admission = options.request_retention;
		exchange = std::make_shared<http::detail::DeferredExchange>(std::move(options), [] {});
		return std::make_unique<http::DeferredReply>(exchange);
	}
};
std::string authority =
	"\"cluster\":\"63000000000000000000000000000000\",\"group\":\"67000000000000000000000000000000\","
	"\"epoch\":\"65000000000000000000000000000000\",";
Writer call(Service &service, std::string path, std::string fields) {
	http::Request r;
	r.method = "POST";
	r.path = std::move(path);
	r.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
	auto sink = service.on_request_body(r);
	sink->write("{" + authority + fields + "}");
	sink->end();
	Writer w;
	service.handle(r, w);
	return w;
}
ServiceOptions options(const std::filesystem::path &directory) {
	ServiceOptions o;
	o.directory = directory;
	o.node.context.cluster[0] = 'c';
	o.node.context.group[0] = 'g';
	o.node.context.epoch[0] = 'e';
	o.node.configuration.cluster = o.node.context.cluster;
	o.node.configuration.configuration = o.node.context.group;
	o.node.configuration.local = 1;
	o.node.configuration.voters = {1};
	o.credentials.push_back({"0123456789abcdef0123456789abcdef", "writer", {"ns"}});
	o.timeout = std::chrono::milliseconds(3000);
	return o;
}
} // namespace
int main() {
	auto directory =
		std::filesystem::current_path() / ".scratch" / ("authority-service-" + std::to_string(getpid()));
	for (auto timeout : {std::chrono::milliseconds::zero(), std::chrono::milliseconds::max()}) {
		auto invalid = options(directory);
		invalid.timeout = timeout;
		bool rejected = false;
		try {
			Service service(std::move(invalid));
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		assert(rejected && !std::filesystem::exists(directory));
	}
	const std::string open = "\"expected_generation\":0,\"nonce\":\"6e000000000000000000000000000000\"";
	const std::string transaction =
		"\"generation\":1,\"sequence\":1,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:key\",\"value\":{"
		"\"type\":\"int\",\"default\":10},\"expected_revision\":0,\"absent_namespace_revision\":0}]";
	{
		Service service(options(directory));
		Writer opened;
		for (unsigned attempt = 0; attempt < 20; ++attempt) {
			opened = call(service, "/v2/open", open);
			if (opened.code == 200) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		if (opened.code != 200) {
			std::cerr << "open failed: " << opened.code << " " << opened.body << "\n";
		}
		assert(opened.code == 200 && opened.body.find("\"generation\":1") != std::string::npos);
		auto duplicate = transaction;
		auto at = duplicate.find("\"default\":10");
		assert(at != std::string::npos);
		duplicate.replace(at, std::string("\"default\":10").size(), "\"default\":10,\"default\":99");
		assert(call(service, "/v2/transactions", duplicate).code == 400);
		auto applied = call(service, "/v2/transactions", transaction);
		assert(applied.code == 200 && applied.body.find("\"revision\":1") != std::string::npos);
		assert(call(service, "/v2/transactions", transaction).body == applied.body);
#if defined(__APPLE__) || defined(__FreeBSD__)
		http::Request probe;
		probe.path = "/v1/health";
		probe.method = "GET";
		Writer health;
		service.handle(probe, health);
		assert(health.code == 200 && health.body.find("\"storage_async\":true") != std::string::npos);
		assert(health.body.find("\"io_native_submitted\":") != std::string::npos &&
			   health.body.find("\"io_native_submitted\":0") == std::string::npos);
#endif

		auto read = call(service, "/v2/read", "\"namespace\":\"ns\",\"keys\":[\"ns:key\"]");
		assert(read.code == 200 && read.body.find("\"default\":10") != std::string::npos &&
			   read.body.find("\"key_revisions\":{\"ns:key\":1}") != std::string::npos);
		assert(call(service, "/v2/read", "\"namespace\":\"private\",\"keys\":[]").code == 400);
		http::Request unauthorized;
		unauthorized.path = "/v2/open";
		unauthorized.method = "POST";
		bool rejected = false;
		try {
			service.on_request_body(unauthorized);
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		assert(rejected && unauthorized.body.empty());
		std::vector<http::Request> held(16);
		for (auto &request : held) {
			request.path = "/v2/open";
			request.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
			service.on_request_body(request);
		}
		http::Request excess;
		excess.path = "/v2/open";
		excess.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
		rejected = false;
		try {
			service.on_request_body(excess);
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		assert(rejected && excess.body.empty());
		held.clear();
	}
	DeferredWriter delayed;
	std::optional<http::Response> held_reply;
	{
		Service service(options(directory));
		Writer retained;
		for (unsigned attempt = 0; attempt < 20; ++attempt) {
			retained = call(service, "/v2/result", "\"generation\":1,\"sequence\":1");
			if (retained.code == 200) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		assert(retained.code == 200 && retained.body.find("\"revision\":1") != std::string::npos);
		assert(call(service, "/v2/transactions", transaction).code == 200);
		http::Request request;
		request.method = "POST";
		request.path = "/v2/read";
		request.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
		auto sink = service.on_request_body(request);
		sink->write("{" + authority + "\"namespace\":\"ns\",\"keys\":[\"ns:key\"]}");
		sink->end();
		service.handle(request, delayed);
		assert(delayed.exchange && delayed.code == 0 && delayed.body.empty());
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (delayed.exchange->phase() == http::detail::DeferredExchange::Phase::Waiting &&
			   std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		held_reply = delayed.exchange->take_response();
		assert(held_reply && held_reply->status == 200 &&
			   held_reply->body.find("\"default\":10") != std::string::npos);
		request.user_data.reset();
		sink.reset();
		assert(!delayed.admission.expired());
		std::string groups;
		for (unsigned index = 0; index != 40; ++index) {
			if (!groups.empty()) {
				groups += ',';
			}
			groups += "\"region:group" + std::to_string(index) + "\":true";
		}
		auto wide = call(service, "/v2/transactions",
						 "\"generation\":1,\"sequence\":2,\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:"
						 "wide\",\"value\":{\"type\":\"bool\",\"default\":true,\"overrides\":{" +
							 groups + "}}}]");
		assert(wide.code == 200);
	}
	assert(!delayed.admission.expired()); // socket IO still owns ingress after service retirement
	held_reply.reset();
	assert(delayed.exchange->settle(http::ReplySettlement::Written));
	assert(delayed.admission.expired());
	{
		auto small = options(directory);
		small.reply_bytes = 1024;
		Service service(std::move(small));
		Writer ordinary;
		auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		do {
			ordinary = call(service, "/v2/read", "\"namespace\":\"ns\",\"keys\":[\"ns:key\"]");
			if (ordinary.code != 503) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		} while (std::chrono::steady_clock::now() < ready_deadline);
		assert(ordinary.code == 200);
		auto oversized = call(service, "/v2/read", "\"namespace\":\"ns\",\"keys\":[\"ns:wide\"]");
		assert(oversized.code == 413 && oversized.body.find("reply_bound") != std::string::npos);
	}
	std::filesystem::remove_all(directory);
	std::cout << "authority ingress, quota and replay passed\n";
}
