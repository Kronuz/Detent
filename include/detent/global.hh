#pragma once

// Section 5 "Global vs injected access": the package exposes global
// functions for source (pyjk-style) compatibility, but all logic lives on
// Client. Only this adapter owns process-level initialization; application
// code that can inject a Client (or a narrower Knobs interface) SHOULD do so
// instead of depending on this namespace.

#include "detent/client.hh"

namespace detent::global {

// Initializes the single process-wide Client. Throws on failure, same as
// Client's constructor. Calling init() twice is a programming error (it
// throws std::logic_error).
void init(InitOptions options);

// Releases the process-wide Client (stops its updater thread, if any).
// Primarily for test teardown; production processes normally never call it.
void shutdown();

[[nodiscard]] Result<bool, KnobError> eval(
	std::string_view name,
	std::optional<std::string_view> hash_val = std::nullopt,
	std::optional<std::string_view> switch_val = std::nullopt);

[[nodiscard]] Result<std::int64_t, KnobError> get(
	std::string_view name,
	std::optional<std::string_view> switch_val = std::nullopt);

[[nodiscard]] bool eval_or_panic(
	std::string_view name,
	std::optional<std::string_view> hash_val = std::nullopt,
	std::optional<std::string_view> switch_val = std::nullopt);

[[nodiscard]] std::int64_t get_or_panic(
	std::string_view name,
	std::optional<std::string_view> switch_val = std::nullopt);

[[nodiscard]] HealthSnapshot health();

} // namespace detent::global
