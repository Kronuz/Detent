#include "service.hh"
#include "support/handler_harness.hh"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <rapidjson/document.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace detent::authority;
namespace {
using Writer = detent_test::Writer;
// A reader holds its own request and loop thread, like another connection.
struct Read {
	http::Request request;
	Writer writer;
	std::unique_ptr<detent_test::Async> run;
	// The handler is back: the request, with its admission, ends here as a connection's does.
	void done() {
		run->finish();
		request.extension.reset();
	}
};
void start_read(Service &service, Read &read, std::string_view token, std::string_view keys) {
	read.request.method = "POST";
	read.request.path = "/v2/read";
	read.request.headers.emplace_back("Authorization", "Bearer " + std::string(token));
	read.run = std::make_unique<detent_test::Async>(service, read.request, read.writer, keys);
}
void wait_released(const Writer &writer) {
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
	Writer w;
	try {
		detent_test::run_handler(service, r, w, "{" + authority + fields + "}");
	} catch (const http::RequestRejected &error) {
		w.code = error.status(); // what the connection answers
		w.body = error.what();
	}
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
	// Core's default timing is the production profile (1-2 s elections); the fixture elects in ~100 ms.
	o.node.timing = {.heartbeat = 20, .rpc_timeout = 40, .election_min = 100, .election_max = 200};
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
	for (auto timeout : {std::chrono::milliseconds::zero(), std::chrono::milliseconds::max()}) {
		auto invalid = options(directory);
		invalid.delivery_timeout = timeout;
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
"delivery_limits":{"timeout_ms":60000},
"credentials":[{"token":"0123456789abcdef0123456789abcdef","producer":"writer","namespaces":["ns"]}]})";
		}
		assert(::chmod(config.c_str(), 0600) == 0);
		const auto funded = load_service_options(config);
		assert(funded.delivery_timeout == std::chrono::seconds(60));
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
				request.extension = service.create_extension(request);
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
			assert(service.create_extension(request));
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
		detent_test::run_handle(service, probe, health);
		// Publication is bounded to four snapshots per second. Wait for a
		// settled write to appear rather than asserting against startup data.
		auto telemetry_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		for (;;) {
			health = Writer{};
			detent_test::run_handle(service, probe, health);
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
			unauthorized.extension = service.create_extension(unauthorized);
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
				invalid.extension = service.create_extension(invalid);
			} catch (const http::RequestRejected &error) {
				denied = true;
			}
			assert(denied && invalid.body.empty());
		}

	}
	{
		Service service(options(directory.string() + "-admission"));
		std::vector<http::Request> held(16);
		for (auto &request : held) {
			request.path = "/v2/open";
			request.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
			request.extension = service.create_extension(request);
		}
		http::Request excess;
		excess.path = "/v2/open";
		excess.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
		bool rejected = false;
		try {
			excess.extension = service.create_extension(excess);
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		assert(rejected && excess.body.empty());
		held.clear();
	}
	std::filesystem::remove_all(directory.string() + "-admission");
	Writer delayed;
	std::optional<Writer::Held> held_reply;
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
		detent_test::intake(service, request, "{" + authority + "\"namespace\":\"ns\",\"keys\":[\"ns:key\"]}");
		detent_test::run_handle(service, request, delayed);
		assert(delayed.exchange && delayed.code == 0 && delayed.body.empty());
		held_reply = delayed.pull();
		assert(held_reply && held_reply->status == 200 &&
			   held_reply->body.find("\"default\":10") != std::string::npos);
		request.extension.reset();
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
	assert(delayed.release(*held_reply));
	held_reply.reset();
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
		auto deferred_read = [&](Read &read) {
			read.request.method = "POST";
			read.request.path = "/v2/read";
			read.request.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
			read.run = std::make_unique<detent_test::Async>(
				service, read.request, read.writer, "{" + authority + "\"namespace\":\"ns\",\"keys\":[\"ns:key\"]}");
		};
		Read first, second;
		deferred_read(first);
		first.done();
		auto first_body = first.writer.pull();
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
			detent_test::run_handle(service, probe, writer);
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
		assert(!second.writer.exchange); // still waiting on the owner for credit
		assert(first.writer.release(*first_body));
		first_body.reset();
		wait_released(first.writer);
		second.done();
		auto second_body = second.writer.pull();
		assert(second_body && second_body->status == 200 && second_body->body == expected);
		assert(second.writer.release(*second_body));
		wait_released(second.writer);
		Read blocked_by, expires;
		deferred_read(blocked_by);
		blocked_by.done();
		auto held = blocked_by.writer.pull();
		assert(held && held->status == 200);
		deferred_read(expires);
		expires.done();
		auto expired = expires.writer.pull();
		assert(expired && expired->status == 503 && expired->body.find("outcome_unknown") != std::string::npos);
		assert(expires.writer.release(*expired));
		wait_released(expires.writer);
		assert(blocked_by.writer.release(*held, http::ReplySettlement::Disconnected));
		wait_released(blocked_by.writer);
	}
	{
		auto constrained = options(directory);
		constrained.requests = 1;
		Service service(std::move(constrained));
		http::Request held;
		held.method = "POST";
		held.path = "/v2/read";
		held.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
		held.extension = service.create_extension(held);
		http::Request rejected;
		rejected.method = held.method;
		rejected.headers = held.headers;
		rejected.path = "/v2/transactions";
		bool pressure = false;
		try {
			rejected.extension = service.create_extension(rejected);
		} catch (const http::RequestRejected &failure) {
			pressure = failure.status() == 503;
		}
		assert(pressure && !rejected.extension && "capacity rejection precedes body and consensus admission");
		held.extension.reset();
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
			detent_test::run_handle(service, probe, writer);
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
		std::vector<Read> readers(16);
		for (auto &reader : readers)
			start_read(service, reader, "fedcba9876543210fedcba9876543210",
					   "{" + authority + "\"namespace\":\"ns\",\"keys\":[" + keys + "]}");
		auto backlog_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		for (;;) {
			Writer writer;
			detent_test::run_handle(service, probe, writer);
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
		unsigned frozen = 0;
		for (unsigned i = 0; i < readers.size(); ++i) {
			readers[i].done();
			auto reply = readers[i].writer.pull();
			assert(reply && reply->status == 200);
			rapidjson::Document body;
			body.Parse(reply->body.c_str());
			assert(!body.HasParseError());
			const auto version = body["version"].GetUint64();
			assert(version == 1 || version == 2);
			frozen += version == 1;
			assert(body["namespace_revision"].GetUint64() == body["version"].GetUint64());
			assert(body["key_revisions"]["ns:heavy0"].GetUint64() == body["version"].GetUint64());
			if (version == 1)
				assert(body["knobs"]["ns:heavy0"]["default"].GetBool());
			else
				assert(body["knobs"]["ns:heavy0"]["default"].GetInt() == 42);
			assert(readers[i].writer.release(*reply));
			wait_released(readers[i].writer);
		}
		assert(frozen == 5); // the capture frozen before the progress commit serves the same five readers as before
		std::vector<Read> terminal_readers(16);
		for (auto &reader : terminal_readers)
			start_read(service, reader, "fedcba9876543210fedcba9876543210",
					   "{" + authority + "\"namespace\":\"ns\",\"keys\":[" + keys + "]}");
		auto terminal_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		for (;;) {
			Writer writer;
			detent_test::run_handle(service, probe, writer);
			rapidjson::Document health;
			health.Parse(writer.body.c_str());
			assert(!health.HasParseError());
			if (health["read_quota_blocked"].GetBool() && health["read_ready_tasks"].GetUint64() >= 10)
				break;
			assert(std::chrono::steady_clock::now() < terminal_deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		// D10: the stop answers every waiting reader by the outcome contract, buffered, and only then
		// does the service go.
		service.begin_shutdown();
		unsigned canceled = 0;
		for (auto &reader : terminal_readers) {
			reader.done();
			if (auto reply = reader.writer.pull()) {
				assert(reply->status == 200);
				assert(reader.writer.release(*reply));
				wait_released(reader.writer);
			} else {
				// Answered buffered because the wait ended on the stop: a reply that was ready, or the
				// outcome contract's 503 (stopping for a read the runtime had not started).
				const auto &body = reader.writer.body;
				assert(reader.writer.code == 200 ||
					   (reader.writer.code == 503 && (body.find("outcome_unknown") != std::string::npos ||
													  body.find("\"stopping\"") != std::string::npos)));
				canceled += reader.writer.code == 503;
			}
		}
		owner.reset();
		assert(canceled >= 10);
	}
	std::filesystem::remove_all(directory);
	std::cout << "authority ingress, quota and replay passed\n";
}
