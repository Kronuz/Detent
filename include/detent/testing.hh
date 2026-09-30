#pragma once

// Section 5 "Test-only client": a thread-local in-memory client for tests,
// so test code never depends on a real defaults file, CLI flags, or a live
// source. Guard-based nesting prevents cross-test leakage (dropping a
// TestGuard restores whatever thread-local client, if any, was active
// before it).

#include "detent/client.hh"

namespace detent::testing {

class TestGuard;

class TestKnobs {
public:
	explicit TestKnobs(Snapshot snapshot);

	// Installs this TestKnobs' client as the thread-local override for
	// detent::global::eval/get on the calling thread. The returned guard
	// must outlive any read that should observe it; dropping it restores the
	// previous thread-local state (nullptr, or an outer TestGuard).
	[[nodiscard]] TestGuard set_thread_local() &&;

private:
	friend class TestGuard;
	std::unique_ptr<Client> client_;
};

class TestGuard {
public:
	~TestGuard();
	TestGuard(const TestGuard&) = delete;
	TestGuard& operator=(const TestGuard&) = delete;
	TestGuard(TestGuard&& other) noexcept;
	TestGuard& operator=(TestGuard&& other) noexcept;

private:
	friend class TestKnobs;
	explicit TestGuard(std::unique_ptr<Client> owned, Client* previous);

	std::unique_ptr<Client> owned_;
	Client* previous_ = nullptr;
	bool active_ = true;
};

} // namespace detent::testing
