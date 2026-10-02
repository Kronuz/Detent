#pragma once

// The host-local agent: one detent::Client kept warm in this process,
// publishing its live snapshot into shared memory (see shm_writer.hh) for
// every local reader (`detent-agent get`, a fresh process each call) to
// evaluate directly against -- no socket, no accept loop, no per-connection
// thread. detent::Client already solves "keep one whole versioned snapshot
// fresh"; the agent's only remaining job is to make that snapshot available
// to other processes on the same host as cheaply as possible.
//
// This replaces an earlier Unix-domain-socket design entirely (greenfield
// rewrite, no wire-protocol compatibility kept): the daemon no longer
// answers requests at all. It polls, parses, validates, merges (all inside
// detent::Client, unchanged), and republishes to shm on every accepted
// update via InitOptions::on_update -- a pure "keep this one shared region
// atomically up to date" role. Every read happens entirely in the reader's
// own process (ShmReader), with zero daemon involvement per call.

#include "detent/client.hh"
#include "shm_writer.hh"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>

namespace detent_agent {

struct Options {
	std::filesystem::path shm_path;
	detent::InitOptions client_options; // however the agent's one Client is configured
	bool system = false;
	std::function<void(const detent::Snapshot&, bool)> on_publish;
	std::function<void(const detent::Metrics&)> on_metrics;
};

class Agent {
public:
	explicit Agent(Options options);
	~Agent();

	Agent(const Agent&) = delete;
	Agent& operator=(const Agent&) = delete;

	// Blocks until stop() is called from another thread, or the process is
	// signaled. All the real work (polling/publishing) already runs on
	// detent::Client's own background thread and fires on every accepted
	// update via on_update -- this just keeps the process (and therefore
	// that thread and the shm mapping) alive until told to stop.
	void serve();
	void stop();

	[[nodiscard]] bool is_serving() const noexcept { return serving_.load(); }
	[[nodiscard]] detent::HealthSnapshot health() const { return client_->health(); }

private:
	Options options_;
	std::mutex publish_mutex_;
	std::unique_ptr<ShmWriter> shm_writer_;
	std::unique_ptr<detent::Client> client_;
	std::atomic<bool> stop_requested_{false};
	std::atomic<bool> serving_{false};
	std::chrono::steady_clock::time_point start_time_{std::chrono::steady_clock::now()};
};

} // namespace detent_agent
