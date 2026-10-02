#include "agent.hh"

#include <chrono>
#include <thread>

namespace detent_agent {

using namespace std::chrono_literals;

Agent::Agent(Options options) : options_(std::move(options)) {
	shm_writer_ = std::make_unique<ShmWriter>(options_.shm_path, options_.system ? 0644 : 0600);

	// Wire on_update *before* constructing Client: the background updater
	// thread can fire it the moment it accepts its first real update, which
	// can happen arbitrarily soon after construction returns.
	std::lock_guard seed_lock(publish_mutex_);
	auto opts = options_.client_options;
	opts.on_update = [this](const detent::Snapshot& snap) {
		std::lock_guard update_lock(publish_mutex_);
		auto published = shm_writer_->publish(snap);
		try { if (options_.on_publish) options_.on_publish(snap, published); } catch (...) { /* Diagnostics are best effort. */ }
	};
	client_ = std::make_unique<detent::Client>(std::move(opts));

	// Seed the shm with defaults (or an empty snapshot) immediately: on_update
	// only fires on a subsequent *accepted source update*, so without this
	// a fresh agent (or one whose source is slow/unreachable) would leave
	// the shm file either absent or stale until the first real update
	// landed, even though the Client itself already has a perfectly valid
	// version-0 snapshot to serve right now.
	if (!shm_writer_->publish(*client_->current_snapshot())) {
		throw std::runtime_error("initial snapshot exceeds shared-memory capacity");
	}
}

Agent::~Agent() { stop(); }

void Agent::serve() {
	serving_.store(true);
	while (!stop_requested_.load()) {
		try { if (options_.on_metrics) options_.on_metrics(client_->metrics()); } catch (...) { /* Diagnostics must not stop the daemon. */ }
		std::this_thread::sleep_for(200ms); // just keeps the process (and Client's thread) alive
	}
	serving_.store(false);
}

void Agent::stop() { stop_requested_.store(true); }

} // namespace detent_agent
