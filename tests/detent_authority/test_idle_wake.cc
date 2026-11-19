// A single-voter authority whose owner sleeps between wakes answers a request handed to it at once: the
// enqueue wakes the owner, which otherwise sleeps until its 250 ms health report. Ten reads, each after
// an idle pause that lets the owner fall asleep, at offsets spread over that interval; at least eight
// must answer within 60 ms. Without the enqueue's wake a read waits for the health report, about
// 125 ms on average, and fewer than eight do.

#include "service.hh"
#include "support/handler_harness.hh"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace detent::authority;
namespace {
using Clock = std::chrono::steady_clock;
using Writer = detent_test::Writer;
const std::string authority = "\"cluster\":\"63000000000000000000000000000000\",\"group\":\"67000000000000000000000000000000\","
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
		w.code = error.status();
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
	o.node.timing = {.heartbeat = 20, .rpc_timeout = 40, .election_min = 100, .election_max = 200};
	o.credentials.push_back({"0123456789abcdef0123456789abcdef", "writer", {"ns"}});
	o.timeout = std::chrono::milliseconds(3000);
	return o;
}
int fail(const std::string &what) {
	std::cerr << "idle wake: " << what << '\n';
	return 1;
}
} // namespace

int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("authority-idle-wake-" + std::to_string(getpid()));
	std::filesystem::create_directories(directory.parent_path());
	std::filesystem::remove_all(directory);
	std::filesystem::remove_all(directory.string() + ".history");
	int status = 0;
	{
		Service service(options(directory));
		Writer opened;
		for (unsigned attempt = 0; attempt < 50 && opened.code != 200; ++attempt) {
			opened = call(service, "/v2/open", "\"expected_generation\":0,\"nonce\":\"6e000000000000000000000000000000\"");
			if (opened.code != 200)
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		if (opened.code != 200)
			return fail("open: " + std::to_string(opened.code) + " " + opened.body);
		const auto applied = call(service, "/v2/transactions",
								  "\"generation\":1,\"sequence\":1,\"namespace\":\"ns\",\"operations\":[{\"key\":"
								  "\"ns:key\",\"value\":{\"default\":10},\"expected_revision\":0,"
								  "\"absent_namespace_revision\":0}]");
		if (applied.code != 200)
			return fail("transaction: " + std::to_string(applied.code) + " " + applied.body);
		std::this_thread::sleep_for(std::chrono::milliseconds(600));
		unsigned prompt = 0;
		std::vector<long long> latencies;
		for (unsigned i = 0; i < 10; ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(300 + 23 * i));
			const auto started = Clock::now();
			const auto read = call(service, "/v2/read", "\"namespace\":\"ns\",\"keys\":[\"ns:key\"]");
			const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
			if (read.code != 200 || read.body.find("\"default\":10") == std::string::npos)
				return fail("read: " + std::to_string(read.code) + " " + read.body);
			latencies.push_back(took);
			prompt += took < 60;
		}
		std::cout << "read latencies after idle (ms):";
		for (auto value : latencies)
			std::cout << ' ' << value;
		std::cout << '\n';
		if (prompt < 8)
			status = fail(std::to_string(prompt) + " of 10 reads answered within 60 ms");
		service.begin_shutdown();
		service.drain();
	}
	std::filesystem::remove_all(directory);
	std::filesystem::remove_all(directory.string() + ".history");
	if (!status)
		std::cout << "idle wake: reads answered at once\n";
	return status;
}
