// Proof for make_http_poll_source's multi-node overload: it starts on ONE
// of the given nodes (not always the first) and, when that node stops
// answering, fails over to another rather than giving up -- see the
// overload's own comment (http_source.hh) for the reasoning (reads don't
// need a leader; any node's ETag/diff is valid against any other).
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "detentd.hh"
#include "detent/client.hh"
#include "detent_http/http_client.hh"
#include "detent_http/http_source.hh"

#include <http_asio.h>

using namespace std::chrono_literals;

namespace {

std::filesystem::path temp_path(std::string_view suffix) {
	static std::mt19937_64 rng{std::random_device{}()};
	return std::filesystem::temp_directory_path() / ("jk_multi_poll_" + std::to_string(rng()) + std::string(suffix));
}


bool wait_until(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (pred()) return true;
		std::this_thread::sleep_for(20ms);
	}
	return pred();
}

void wait_for_server_ready(std::uint16_t port, std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	for (;;) {
		try {
			(void)detent_http::http_get("127.0.0.1", port, "/v1/health", 500ms);
			return;
		} catch (const std::exception&) {
			if (std::chrono::steady_clock::now() >= deadline) throw;
			std::this_thread::sleep_for(10ms);
		}
	}
}

struct Node {
	detentd::Options options;
	std::unique_ptr<detentd::Distributor> app;
	std::unique_ptr<http::HttpAsioService> service;
	std::filesystem::path persist;
	unsigned short port = 0;

	explicit Node(std::string knob_default_literal) {
		persist = temp_path(".json");
		options.persist_path = persist;
		app = std::make_unique<detentd::Distributor>(options);
		const auto deadline = std::chrono::steady_clock::now() + 3s;
		for (;;) {
			asio::io_context io;
			asio::ip::tcp::acceptor reservation(io,
			                                    asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
			port = reservation.local_endpoint().port();
			service = std::make_unique<http::HttpAsioService>(*app, 2, 4, 256);
			http::AsioBindOptions binding;
			binding.address = asio::ip::address_v4::loopback().to_string();
			service->set_bind_options(binding);
			service->enable_conditional();
			reservation.close();
			try {
				service->start(port);
				break;
			} catch (const asio::system_error &error) {
				service.reset();
				if (error.code() != asio::error::address_in_use ||
				    std::chrono::steady_clock::now() >= deadline)
					throw;
			}
		}
		std::this_thread::sleep_for(50ms);
		wait_for_server_ready(port, 3s);

		// Each node's own snapshot names itself in the knob's default, so a
		// client's read alone (no server-side introspection needed) proves
		// which node actually answered.
		auto put = detent_http::http_put(
			"127.0.0.1", port, "/v1/snapshot",
			R"({"schema_version":1,"version":1,"knobs":{"probe:origin":{"type":"int","default":)" +
				knob_default_literal + R"(}}})",
			2s);
		assert(put.status == 200);
	}

	void stop() {
		if (service) service->stop();
		service.reset();
		app.reset();
	}

	~Node() {
		stop();
		std::filesystem::remove(persist);
	}
};

} // namespace

int main() {
	Node node0("0");
	Node node1("1");

	auto defaults_path = temp_path("_defaults.json");
	{
		std::ofstream out(defaults_path, std::ios::binary);
		out << R"({"schema_version":1,"knobs":{"probe:origin":{"type":"int","default":-1}}})";
	}

	std::vector<std::pair<std::string, std::uint16_t>> nodes{{"127.0.0.1", node0.port}, {"127.0.0.1", node1.port}};

	detent::InitOptions client_opts;
	client_opts.defaults_path = defaults_path;
	client_opts.source_poll = detent_http::make_http_poll_source(nodes, "/v1/snapshot", 2s);
	client_opts.poll_interval = 30ms;
	detent::Client client(std::move(client_opts));

	bool got_initial = wait_until([&] { return client.get("probe:origin").value() != -1; }, 3s);
	assert(got_initial);
	auto first_origin = client.get("probe:origin").value();
	assert(first_origin == 0 || first_origin == 1);
	std::printf("started against node%lld OK\n", static_cast<long long>(first_origin));

	// Stop whichever node actually answered -- the other one is still up,
	// so a correct failover keeps the client working; a client pinned to
	// the dead node would stall here instead.
	if (first_origin == 0) {
		node0.stop();
	} else {
		node1.stop();
	}

	// Bump the surviving node's version so client.get() above can't just be
	// seeing cached state -- a real new poll has to succeed against it.
	auto& survivor = (first_origin == 0) ? node1 : node0;
	auto put2 = detent_http::http_put(
		"127.0.0.1", survivor.port, "/v1/snapshot",
		R"({"schema_version":1,"version":2,"knobs":{"probe:origin":{"type":"int","default":)" +
			std::to_string(first_origin) + R"(}}})",
		2s);
	assert(put2.status == 200);

	bool failed_over = wait_until([&] { return client.health().accepted_version == 2; }, 5s);
	assert(failed_over);
	std::puts("failover to the surviving node after the current one stopped OK");

	std::filesystem::remove(defaults_path);
	return 0;
}
