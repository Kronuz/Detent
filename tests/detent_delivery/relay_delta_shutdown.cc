// A relay bundle takes a delta after its snapshot and the coordinator then closes: the run must drain.
// PHASE is where the close lands: published (the delta is selected), receiving (the delta reply is in flight),
// building or publishing (the coordinator is in that phase for the delta).
// Uses only the scripted upstream, so it builds on a tree without the hash counters.
// usage: test_delivery_relay_delta_shutdown CERTIFICATES [KNOBS [PHASE]]
#include "scripted_upstream.hh"

using namespace scripted_upstream;

namespace {
// Polls once per coordinator turn, so a phase that lasts a few turns is still seen.
task::task<bool> reached(task::asio_scheduler &scheduler, std::chrono::seconds limit, const auto &predicate) {
	const auto deadline = std::chrono::steady_clock::now() + limit;
	while (!predicate()) {
		if (std::chrono::steady_clock::now() >= deadline)
			co_return false;
		co_await task::yield();
	}
	co_return true;
}
} // namespace

int main(int argc, char **argv) {
	try {
		check(argc >= 2 && argc <= 4);
		const fs::path certificates(argv[1]);
		if (argc >= 3)
			knobs = std::atoi(argv[2]);
		const std::string phase = argc == 4 ? argv[3] : "published";
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
					  if (phase == "receiving")
						  server.delta_delay_ms = 1000;
					  server.resume = Server::Resume::Delta;
					  const auto in_phase = [&](t::CoordinatorPhase wanted) {
						  return [&, wanted] { return server.deltas == 1 && coordinator->status().phase == wanted; };
					  };
					  bool ok = false;
					  if (phase == "published")
						  ok = co_await until(scheduler, 60s, [&] {
							  const auto status = coordinator->status();
							  return status.recoverable && status.recoverable->revision == 2;
						  });
					  else if (phase == "receiving")
						  ok = co_await reached(scheduler, 60s, [&] { return server.deltas == 1; });
					  else if (phase == "building")
						  ok = co_await reached(scheduler, 60s, in_phase(t::CoordinatorPhase::Building));
					  else if (phase == "publishing")
						  ok = co_await reached(scheduler, 60s, in_phase(t::CoordinatorPhase::Publishing));
					  check(explain(ok, coordinator, server));
					  std::cout << "delta " << phase << "; closing" << std::endl;
				  });
		std::cout << "drained\nok\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "test_delivery_relay_delta_shutdown: " << error.what() << '\n';
		return 1;
	}
}
