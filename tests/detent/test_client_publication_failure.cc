// A refused allocation while publishing a patch must leave the client's patch
// base at its last accepted version. The source serves diffs relative to that
// version: v2 adds t:b, v3 reverts it and adds t:c, so every run must end at
// {t:a, t:c} whether or not v2 was ever accepted.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <optional>
#include <thread>

#include "detent/client.hh"

namespace {
thread_local long budget = -1;
}

void* operator new(std::size_t bytes) {
	if (budget == 0) {
		budget = -1;
		throw std::bad_alloc();
	}
	if (budget > 0)
		--budget;
	if (auto p = std::malloc(bytes ? bytes : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace detent;
using namespace std::chrono_literals;

namespace {

constexpr const char* kV1 =
	R"({"schema_version":1,"version":1,"knobs":{"t:a":{"type":"int","default":1}}})";
constexpr const char* kV2FromV1 = R"({"schema_version":1,"version":2,"knobs":{"t:b":{"type":"int","default":2}}})";
constexpr const char* kV3FromV1 = R"({"schema_version":1,"version":3,"knobs":{"t:c":{"type":"int","default":3}}})";
constexpr const char* kV3FromV2 =
	R"({"schema_version":1,"version":3,"knobs":{"t:b":null,"t:c":{"type":"int","default":3}}})";

// Returns true when allocation `position` of the v2 publication was refused.
bool run(long position) {
	bool armed = false, refused = false;
	InitOptions options;
	options.poll_interval = 1ms;
	options.source_id = "test";
	options.source_poll = [&, step = 0](std::uint64_t after) mutable -> std::optional<InitOptions::SourcePayload> {
		if (armed) {
			refused = budget == -1;
			budget = -1;
			armed = false;
		}
		if (after == 0)
			return InitOptions::SourcePayload{kV1, false};
		if (after == 1 && step++ == 0) {
			InitOptions::SourcePayload payload{kV2FromV1, true};
			armed = true;
			budget = position;
			return payload;
		}
		if (after == 1)
			return InitOptions::SourcePayload{kV3FromV1, true};
		if (after == 2)
			return InitOptions::SourcePayload{kV3FromV2, true};
		return std::nullopt;
	};
	Client client(std::move(options));
	const auto deadline = std::chrono::steady_clock::now() + 30s;
	while (client.current_snapshot()->version < 3) {
		assert(std::chrono::steady_clock::now() < deadline);
		std::this_thread::sleep_for(1ms);
	}
	const auto snapshot = client.current_snapshot();
	if (snapshot->knobs.size() != 2 || !snapshot->knobs.contains("t:a") || !snapshot->knobs.contains("t:c")) {
		std::fprintf(stderr, "FAILED: position %ld published %zu knobs, t:b %s\n", position, snapshot->knobs.size(),
					 snapshot->knobs.contains("t:b") ? "present" : "absent");
		std::abort();
	}
	return refused;
}

} // namespace

int main() {
	long refused = 0;
	for (long position = 0; run(position); ++position)
		++refused;
	assert(refused > 0);
	std::printf("PASS client publication failure (%ld refused positions)\n", refused);
	return 0;
}
