#pragma once
// Test-only seams of the reader core (reader.md T3, T6), compiled only with DETENT_READER_TEST_HOOKS.
// The release library has neither the hooks nor these symbols.
#include "detent/reader.h"
#include <cstdint>

namespace detent::reader::testing {
enum class Window {
	ChangeMapped, // change was mapped, before the core records its identity
	BeforePin,  // a lookup loaded `current`, before its increment
	Pinned,     // a lookup holds its slot, before it reads the view
	Refreshing, // a thread took `refreshing`, before any work
	AfterFree,  // a publish freed the next slot's view, before it attaches
	Attaching,  // a refresh read the store word and table, before it attaches a generation
	Freeing,    // a publish found an old slot unpinned, before it clears and frees it
};
// Runs at each window on the thread that reaches it. Install before any call, never concurrently.
void install(void (*)(Window, void *) noexcept, void *) noexcept;
struct Stats {
	std::uint64_t live_views = 0, publishes = 0, refused = 0, attached = 0, generations = 0;
	std::uint64_t current = 0;
	std::uint64_t pins[4] = {}; // each ring slot's pin count
};
Stats stats(detent_reader *) noexcept;
// What the calling thread's last failed refresh raised.
const char *last_failure() noexcept;
} // namespace detent::reader::testing
