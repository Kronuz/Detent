// Fresh durable Service: 128 knobs × 256 overrides and five strong reads.
// Run without competing builds. --escaped exercises worst-case JSON expansion.
// Owner histograms cover setup, reads and ordinary reclamation, before Service
// destruction. Shutdown is timed separately; read timings are wall time.
#include "service.hh"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <stdexcept>
#include <sys/resource.h>
#include <thread>
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
int main(int argc, char **argv) {
	auto require = [](bool okay) {
		if (!okay)
			throw std::runtime_error("read benchmark validation failed");
	};

	auto directory =
		std::filesystem::current_path() / ".scratch" / ("authority-read-bench-" + std::to_string(getpid()));
	{
		auto o = options(directory);
		o.timeout = std::chrono::seconds(10);
		auto owner = std::make_unique<Service>(std::move(o));
		auto &service = *owner;
		Writer opened;
		for (unsigned attempt = 0; attempt < 100; ++attempt) {
			opened = call(service, "/v2/open",
						  "\"expected_generation\":0,\"nonce\":\"6e000000000000000000000000000000\"");
			if (opened.code == 200)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		require(opened.code == 200);
		std::string groups, keys;
		const bool escaped = argc > 1 && std::string_view(argv[1]) == "--escaped";
		for (unsigned i = 0; i < 256; ++i) {
			if (i)
				groups += ',';
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
			const auto key = "group" + std::to_string(i) + std::string(46, escaped ? '\x01' : 'k');
			writer.String(key.data(), key.size());
			groups.append(buffer.GetString(), buffer.GetSize());
			groups += ":true";
		}
		for (unsigned i = 0; i < 128; ++i) {
			const auto key = "ns:large" + std::to_string(i);
			if (i)
				keys += ',';
			keys += '"' + key + '"';
			auto reply = call(service, "/v2/transactions",
							  "\"generation\":1,\"sequence\":" + std::to_string(i + 1) +
								  ",\"namespace\":\"ns\",\"operations\":[{\"key\":\"" + key +
								  "\",\"value\":{\"type\":\"bool\",\"default\":true,\"overrides\":{" +
								  groups + "}}}]");
			if (reply.code != 200)
				std::cerr << reply.code << ' ' << reply.body << '\n';
			require(reply.code == 200);
		}
		for (unsigned trial = 0; trial < 5; ++trial) {
			auto start = std::chrono::steady_clock::now();
			auto reply = call(service, "/v2/read", "\"namespace\":\"ns\",\"keys\":[" + keys + "]");
			auto ns =
				std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
					.count();
			require(reply.code == 200);
			std::cout << "{\"trial\":" << trial << ",\"reply_bytes\":" << reply.body.size()
					  << ",\"read_wall_ns\":" << ns << "}\n";
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		http::Request probe;
		probe.path = "/v1/health";
		probe.method = "GET";
		Writer health;
		service.handle(probe, health);
		std::cout << health.body << '\n';
		const auto shutdown_started = std::chrono::steady_clock::now();
		owner.reset();
		const auto shutdown_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
									 std::chrono::steady_clock::now() - shutdown_started)
									 .count();
		std::cout << "{\"service_shutdown_wall_ns\":" << shutdown_ns << "}\n";
	}
	rusage usage{};
	getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
	const auto rss = usage.ru_maxrss;
#else
	const auto rss = usage.ru_maxrss * 1024;
#endif
	const auto user_ns =
		std::uint64_t(usage.ru_utime.tv_sec) * 1000000000 + std::uint64_t(usage.ru_utime.tv_usec) * 1000;
	const auto system_ns =
		std::uint64_t(usage.ru_stime.tv_sec) * 1000000000 + std::uint64_t(usage.ru_stime.tv_usec) * 1000;
	std::cout << "{\"process_user_cpu_ns\":" << user_ns << ",\"process_system_cpu_ns\":" << system_ns
			  << ",\"max_rss_bytes\":" << rss << "}\n";
	std::filesystem::remove_all(directory);
}
