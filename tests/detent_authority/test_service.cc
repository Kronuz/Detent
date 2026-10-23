#include "service.hh"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <rapidjson/document.h>
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
void wait_released(const DeferredWriter &writer) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!writer.admission.expired()) {
		assert(std::chrono::steady_clock::now() < deadline);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
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
	std::filesystem::create_directories(directory.parent_path());
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
	{
		auto config = directory.parent_path() / (directory.filename().string() + ".json");
		for (const auto *flag : {"true", "false", "\"false\"", "0", "null"}) {
			{
				std::ofstream file(config);
				file << "{" << authority << "\"directory\":\"read-only-state\",\"credentials\":[{"
					 << "\"token\":\"0123456789abcdef0123456789abcdef\",\"producer\":\"agent\","
					 << "\"namespaces\":[\"ns\"],\"publish\":" << flag << "}]}";
			}
			assert(::chmod(config.c_str(), 0600) == 0);
			bool accepted = false;
			try {
				auto loaded = load_service_options(config);
				assert(loaded.credentials.front().publish == (std::string_view(flag) == "true"));
				accepted = true;
			} catch (const std::invalid_argument &) {
			}
			assert(accepted == (std::string_view(flag) == "true" || std::string_view(flag) == "false"));
		}
		for (const auto *capacity : {
			 R"({"keys":5000000,"state_bytes":268435456,"image_bytes":402653184})",
			 R"({"keys":10000000,"state_bytes":268435456,"image_bytes":402653184})"}) {
			{
				std::ofstream file(config);
				file << "{" << authority << "\"directory\":\"capacity-state\",\"application_limits\":"
					 << capacity << ",\"credentials\":[{\"token\":\"0123456789abcdef0123456789abcdef\","
					 << "\"producer\":\"writer\",\"namespaces\":[\"ns\"]}]}";
			}
			assert(::chmod(config.c_str(), 0600) == 0);
			const auto configured = load_service_options(config);
			assert(configured.node.application.keys >= 5000000);
			assert(configured.node.application.state_bytes == 268435456);
			assert(configured.node.application.image_bytes == 402653184);
			assert(semantic_profile(configured.node.application) != semantic_profile(Limits{}));
		}
		{
			std::ofstream file(config);
			file << "{" << authority << R"("directory":"large-capacity-state",
"application_limits":{"keys":10000000,"state_bytes":1073741824,"image_bytes":1610612736},
"journal_limits":{"hard_bytes":4294967296,"replacement_bytes":2147483648},
"credentials":[{"token":"0123456789abcdef0123456789abcdef","producer":"writer","namespaces":["ns"]}]})";
		}
		assert(::chmod(config.c_str(), 0600) == 0);
		const auto funded = load_service_options(config);
		assert(funded.node.application.image_bytes == 1610612736 &&
			   funded.node.storage.replacement_pool.logical_bytes == 2147483648);
		std::filesystem::remove(config);
	}
	{
		auto read_only = options(directory.string() + "-read-only");
		read_only.credentials.front().publish = false;
		Service service(std::move(read_only));
		for (const auto *path : {"/v2/open", "/v2/close", "/v2/transactions"}) {
			http::Request request;
			request.method = "POST";
			request.path = path;
			request.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
			bool rejected = false;
			try {
				service.on_request_body(request);
			} catch (const http::RequestRejected &error) {
				rejected = error.status() == 403;
			}
			assert(rejected);
		}
		for (const auto *path : {"/v2/read", "/v2/distribution"}) {
			http::Request request;
			request.method = "POST";
			request.path = path;
			request.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
			assert(service.on_request_body(request));
		}
	}
	std::filesystem::remove_all(directory.string() + "-read-only");
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
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
		http::Request probe;
		probe.path = "/v1/health";
		probe.method = "GET";
		Writer health;
		service.handle(probe, health);
		// Publication is bounded to four snapshots per second. Wait for a
		// settled write to appear rather than asserting against startup data.
		auto telemetry_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		for (;;) {
			health = Writer{};
			service.handle(probe, health);
			rapidjson::Document sample;
			sample.Parse(health.body.c_str());
			assert(!sample.HasParseError() && sample.IsObject());
			assert(sample.HasMember("duration_bucket_upper_bounds_ns"));
			assert(sample["duration_bucket_upper_bounds_ns"].Size() == 14);
			assert(sample.HasMember("snapshot_age_ms") && sample["snapshot_age_ms"].IsUint64());
			if (sample["io_job_settlement_ns"]["count"].GetUint64() > 0 &&
				sample["publisher_admitted_to_ready_ns"]["count"].GetUint64() > 0) {
				assert(sample["owner_turn_ns"]["count"].GetUint64() > 0);
				assert(sample["io_job_fence_observed_ns"]["count"].GetUint64() == 0);
				break;
			}
			assert(std::chrono::steady_clock::now() < telemetry_deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
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
		for (const auto &token : {std::string("0123456789abcdef0123456789abcdee"),
								  std::string("0123456789abcdef0123456789abcdeff"), std::string(129, 'a')}) {
			http::Request invalid;
			invalid.path = "/v2/open";
			invalid.headers.emplace_back("Authorization", "Bearer " + token);
			bool denied = false;
			try {
				service.on_request_body(invalid);
			} catch (const http::RequestRejected &error) {
				denied = true;
			}
			assert(denied && invalid.body.empty());
		}

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
		// Counted reads wait for existing egress credit without allocating a
		// second body. Original write settlement releases the same reservation.
		auto deferred_read = [&](DeferredWriter &writer) {
			http::Request request;
			request.method = "POST";
			request.path = "/v2/read";
			request.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
			auto sink = service.on_request_body(request);
			sink->write("{" + authority + "\"namespace\":\"ns\",\"keys\":[\"ns:key\"]}");
			sink->end();
			service.handle(request, writer);
			assert(writer.exchange);
		};
		auto wait_ready = [](DeferredWriter &writer) {
			auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (writer.exchange->phase() == http::detail::DeferredExchange::Phase::Waiting) {
				assert(std::chrono::steady_clock::now() < deadline);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		};
		DeferredWriter first, second;
		deferred_read(first);
		wait_ready(first);
		auto first_body = first.exchange->take_response();
		assert(first_body && first_body->status == 200);
		const auto expected = first_body->body;
		assert(2 * (expected.size() + 256) > 1024);
		deferred_read(second);
		http::Request probe;
		probe.method = "GET";
		probe.path = "/v1/health";
		auto quota_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		for (;;) {
			Writer writer;
			service.handle(probe, writer);
			rapidjson::Document health;
			health.Parse(writer.body.c_str());
			assert(!health.HasParseError());
			if (health["read_quota_blocked"].GetBool() && health["read_captures"].GetUint64() == 1 &&
				health["reserved_reply_bytes"].GetUint64() == expected.size() + 256) {
				assert(health["read_buffer_reserved_bytes"].GetUint64() == 0);
				break;
			}
			assert(std::chrono::steady_clock::now() < quota_deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		assert(second.exchange->phase() == http::detail::DeferredExchange::Phase::Waiting);
		first_body.reset();
		assert(first.exchange->settle(http::ReplySettlement::Written));
		wait_released(first);
		wait_ready(second);
		auto second_body = second.exchange->take_response();
		assert(second_body && second_body->status == 200 && second_body->body == expected);
		second_body.reset();
		assert(second.exchange->settle(http::ReplySettlement::Written));
		wait_released(second);
		DeferredWriter blocked_by, expires;
		deferred_read(blocked_by);
		wait_ready(blocked_by);
		auto held = blocked_by.exchange->take_response();
		assert(held && held->status == 200);
		deferred_read(expires);
		wait_ready(expires);
		auto expired = expires.exchange->take_response();
		assert(expired && expired->status == 503 &&
			   expired->body.find("outcome_unknown") != std::string::npos);
		expired.reset();
		assert(expires.exchange->settle(http::ReplySettlement::Written));
		wait_released(expires);
		held.reset();
		assert(blocked_by.exchange->settle(http::ReplySettlement::Disconnected));
		wait_released(blocked_by);
	}
	{
		auto constrained = options(directory);
		constrained.requests = 1;
		Service service(std::move(constrained));
		http::Request held;
		held.method = "POST";
		held.path = "/v2/read";
		held.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
		auto sink = service.on_request_body(held);
		http::Request rejected;
		rejected.method = held.method;
		rejected.headers = held.headers;
		rejected.path = "/v2/transactions";
		bool pressure = false;
		try {
			service.on_request_body(rejected);
		} catch (const http::RequestRejected &failure) {
			pressure = failure.status() == 503;
		}
		assert(pressure && !rejected.user_data && "capacity rejection precedes body and consensus admission");
		sink.reset();
		held.user_data.reset();
		Writer committed;
		for (unsigned attempt = 0; attempt < 100; ++attempt) {
			committed = call(service, "/v2/transactions",
							 "\"generation\":1,\"sequence\":3,\"namespace\":\"ns\",\"operations\":[{\"key\":"
							 "\"ns:pressure\",\"value\":{\"type\":\"bool\",\"default\":true}}]");
			if (committed.code != 503) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		assert(committed.code == 200 &&
			   "rejected headers consume no producer sequence and released credit is reusable");
	}
	{
		auto heavy_directory = directory / "heavy";
		auto heavy_options = options(heavy_directory);
		heavy_options.reply_bytes = 1024 * 1024;
		heavy_options.timeout = std::chrono::seconds(10);
		heavy_options.credentials.push_back({"fedcba9876543210fedcba9876543210", "reader", {"ns"}});
		auto owner = std::make_unique<Service>(std::move(heavy_options));
		auto &service = *owner;
		Writer opened;
		for (unsigned attempt = 0; attempt < 100; ++attempt) {
			opened = call(service, "/v2/open", open);
			if (opened.code == 200)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		assert(opened.code == 200);
		std::string groups;
		for (unsigned i = 0; i < 31; ++i) {
			if (!groups.empty())
				groups += ',';
			groups += "\"group" + std::to_string(i) + std::string(46, 'k') + "\":true";
		}
		std::string operations;
		for (unsigned i = 0; i < 128; ++i) {
			if (!operations.empty())
				operations += ',';
			operations += "{\"key\":\"ns:heavy" + std::to_string(i) +
						  "\",\"value\":{\"type\":\"bool\",\"default\":true,\"overrides\":{" + groups + "}}}";
		}
		auto large = [&](unsigned sequence) {
			return "\"generation\":1,\"sequence\":" + std::to_string(sequence) +
				   ",\"namespace\":\"ns\",\"operations\":[" + operations + "]";
		};
		assert(large(1).size() > 240000 && large(1).size() + authority.size() + 2 < 256 * 1024);
		auto changed = call(service, "/v2/transactions", large(1));
		assert(changed.code == 200 && changed.body.find("\"status\":\"applied\"") != std::string::npos);
		auto unchanged = call(service, "/v2/transactions", large(2));
		assert(unchanged.code == 200 && unchanged.body.find("\"status\":\"no_change\"") != std::string::npos);
		assert(call(service, "/v2/transactions", large(1)).body == changed.body);
		http::Request probe;
		probe.path = "/v1/health";
		probe.method = "GET";
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		for (;;) {
			Writer writer;
			service.handle(probe, writer);
			rapidjson::Document health;
			health.Parse(writer.body.c_str());
			assert(!health.HasParseError());
			if (health["requests"].GetUint64() == 0 && health["retired_state_values"].GetUint64() == 0 &&
				health["retired_publisher_tasks"].GetUint64() == 0) {
				assert(health["retired_state_value_bytes"].GetUint64() == 0 &&
					   health["retired_publisher_bytes"].GetUint64() == 0);
				break;
			}
			assert(std::chrono::steady_clock::now() < deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		// Many verified reads retain tasks, while one frozen capture waits for
		// four original writes. A separate publisher remains able to commit.
		std::string keys;
		for (unsigned i = 0; i < 128; ++i) {
			if (i)
				keys += ',';
			keys += "\"ns:heavy" + std::to_string(i) + "\"";
		}
		std::vector<DeferredWriter> readers(16);
		for (auto &reader : readers) {
			http::Request request;
			request.method = "POST";
			request.path = "/v2/read";
			request.headers.emplace_back("Authorization", "Bearer fedcba9876543210fedcba9876543210");
			auto sink = service.on_request_body(request);
			sink->write("{" + authority + "\"namespace\":\"ns\",\"keys\":[" + keys + "]}");
			sink->end();
			service.handle(request, reader);
			assert(reader.exchange);
		}
		auto backlog_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		for (;;) {
			Writer writer;
			service.handle(probe, writer);
			rapidjson::Document health;
			health.Parse(writer.body.c_str());
			assert(!health.HasParseError());
			if (health["read_quota_blocked"].GetBool() && health["read_ready_tasks"].GetUint64() >= 10 &&
				health["read_captures"].GetUint64() == 1 &&
				health["reserved_reply_bytes"].GetUint64() > 900000) {
				assert(health["read_buffer_reserved_bytes"].GetUint64() == 0);
				break;
			}
			assert(std::chrono::steady_clock::now() < backlog_deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		auto progress = call(service, "/v2/transactions",
							 "\"generation\":1,\"sequence\":3,\"namespace\":\"ns\",\"operations\":[{\"key\":"
							 "\"ns:heavy0\",\"value\":{\"type\":\"int\",\"default\":42}}]");
		assert(progress.code == 200 && progress.body.find("\"revision\":2") != std::string::npos);
		for (unsigned i = 0; i < readers.size(); ++i) {
			auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (readers[i].exchange->phase() == http::detail::DeferredExchange::Phase::Waiting) {
				assert(std::chrono::steady_clock::now() < ready_deadline);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			auto reply = readers[i].exchange->take_response();
			assert(reply && reply->status == 200);
			rapidjson::Document body;
			body.Parse(reply->body.c_str());
			assert(!body.HasParseError());
			assert(body["version"].GetUint64() == (i < 5 ? 1 : 2));
			assert(body["namespace_revision"].GetUint64() == body["version"].GetUint64());
			assert(body["key_revisions"]["ns:heavy0"].GetUint64() == body["version"].GetUint64());
			if (i < 5)
				assert(body["knobs"]["ns:heavy0"]["default"].GetBool());
			else
				assert(body["knobs"]["ns:heavy0"]["default"].GetInt() == 42);
			reply.reset();
			assert(readers[i].exchange->settle(http::ReplySettlement::Written));
			wait_released(readers[i]);
		}
		std::vector<DeferredWriter> terminal_readers(16);
		for (auto &reader : terminal_readers) {
			http::Request request;
			request.method = "POST";
			request.path = "/v2/read";
			request.headers.emplace_back("Authorization", "Bearer fedcba9876543210fedcba9876543210");
			auto sink = service.on_request_body(request);
			sink->write("{" + authority + "\"namespace\":\"ns\",\"keys\":[" + keys + "]}");
			sink->end();
			service.handle(request, reader);
			assert(reader.exchange);
		}
		auto terminal_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		for (;;) {
			Writer writer;
			service.handle(probe, writer);
			rapidjson::Document health;
			health.Parse(writer.body.c_str());
			assert(!health.HasParseError());
			if (health["read_quota_blocked"].GetBool() && health["read_ready_tasks"].GetUint64() >= 10)
				break;
			assert(std::chrono::steady_clock::now() < terminal_deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		owner.reset();
		unsigned canceled = 0;
		for (auto &reader : terminal_readers) {
			auto reply = reader.exchange->take_response();
			assert(reply && (reply->status == 200 || reply->status == 503));
			canceled += reply->status == 503;
			reply.reset();
			assert(reader.exchange->settle(http::ReplySettlement::Written));
			wait_released(reader);
		}
		assert(canceled >= 10);
	}
	std::filesystem::remove_all(directory);
	std::cout << "authority ingress, quota and replay passed\n";
}
