// A relay bundle takes a delta after its snapshot and the coordinator then closes: the run must drain.
// Uses only the scripted upstream, so it builds on a tree without the hash counters.
// usage: test_delivery_relay_delta_shutdown CERTIFICATES [KNOBS]
#include "scripted_upstream.hh"

using namespace scripted_upstream;

int main(int argc, char **argv) {
	try {
		check(argc == 2 || argc == 3);
		const fs::path certificates(argv[1]);
		if (argc == 3)
			knobs = std::atoi(argv[2]);
		const auto root = scratch("relay-delta-shutdown");
		struct Cleanup {
			fs::path path;
			~Cleanup() { fs::remove_all(path); }
		} cleanup{root};
		const auto store = root / "store";
		fs::create_directories(store);
		fs::permissions(store, fs::perms::owner_all);
		Server server(certificates);
		run_phase(certificates, store, server, {.scrub_rate = 0, .relay = true, .fresh = true},
				  [&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
					  check(explain(co_await until(scheduler, 120s, [&] {
						  const auto status = coordinator->status();
						  return status.acknowledged == cursor() && status.recoverable == cursor();
					  }), coordinator, server));
					  server.resume = Server::Resume::Delta;
					  check(explain(co_await until(scheduler, 60s, [&] {
						  const auto status = coordinator->status();
						  return status.recoverable && status.recoverable->revision == 2;
					  }), coordinator, server));
					  std::cout << "delta published; closing" << std::endl;
				  });
		std::cout << "drained\nok\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "test_delivery_relay_delta_shutdown: " << error.what() << '\n';
		return 1;
	}
}
