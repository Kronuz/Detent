// A single-voter authority starts and stops without sleeping through its own work. Start, replay and
// stop are steps the owner advances by visiting the node again; only storage or history I/O in flight
// may end a visit in a sleep, until that completion's wake. A step that slept whenever the applied
// index stood still waits the 250 ms bound once per budgeted step: replaying a few transactions then
// takes seconds, and every stop ends in one full sleep. Three restarts must each start within 1 s and
// stop within 200 ms.

#include "service.hh"
#include "support/handler_harness.hh"
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
long long since(Clock::time_point started) {
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
}
int fail(const std::string &what) {
	std::cerr << "restart wake: " << what << '\n';
	return 1;
}
} // namespace

int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("authority-restart-wake-" + std::to_string(getpid()));
	std::filesystem::create_directories(directory.parent_path());
	std::filesystem::remove_all(directory);
	std::filesystem::remove_all(directory.string() + ".history");
	int status = 0;
	unsigned revision = 0;
	for (unsigned round = 0; round < 4; ++round) {
		auto started = Clock::now();
		Service service(options(directory));
		const auto start_ms = since(started);
		if (round == 0) {
			Writer opened;
			for (unsigned attempt = 0; attempt < 50 && opened.code != 200; ++attempt) {
				opened = call(service, "/v2/open", "\"expected_generation\":0,\"nonce\":\"6e000000000000000000000000000000\"");
				if (opened.code != 200)
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (opened.code != 200)
				return fail("open: " + std::to_string(opened.code) + " " + opened.body);
		} else {
			// A restarted voter answers once it leads again (an election of 100 to 200 ms).
			Writer read;
			for (unsigned attempt = 0; attempt < 100 && read.code != 200; ++attempt) {
				read = call(service, "/v2/read", "\"namespace\":\"ns\",\"keys\":[\"ns:key\"]");
				if (read.code != 200)
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (read.code != 200)
				return fail("read after restart: " + std::to_string(read.code) + " " + read.body);
		}
		// Each round commits more entries for the next start to replay.
		for (unsigned i = 0; i < 4; ++i) {
			const auto sequence = std::to_string(round * 4 + i + 1);
			const auto written = call(service, "/v2/transactions",
									  "\"generation\":1,\"sequence\":" + sequence +
										  ",\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:key\",\"value\":{\"default\":" +
										  sequence + "},\"expected_revision\":" + std::to_string(revision) +
										  (revision ? "" : ",\"absent_namespace_revision\":0") + "}]");
			const auto at = written.body.find("\"revision\":");
			if (written.code != 200 || at == std::string::npos)
				return fail("transaction " + sequence + ": " + std::to_string(written.code) + " " + written.body);
			revision = static_cast<unsigned>(std::stoul(written.body.substr(at + 11)));
		}
		started = Clock::now();
		service.begin_shutdown();
		service.drain();
		const auto stop_ms = since(started);
		std::cout << "round " << round << ": start " << start_ms << " ms, stop " << stop_ms << " ms\n";
		if (round && start_ms >= 1000)
			status = fail("round " + std::to_string(round) + " started in " + std::to_string(start_ms) + " ms");
		if (stop_ms >= 200)
			status = fail("round " + std::to_string(round) + " stopped in " + std::to_string(stop_ms) + " ms");
	}
	std::filesystem::remove_all(directory);
	std::filesystem::remove_all(directory.string() + ".history");
	if (!status)
		std::cout << "restart wake: starts and stops without sleeping through work\n";
	return status;
}
