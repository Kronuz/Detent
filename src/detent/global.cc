#include "detent/global.hh"
#include "detent/detail/test_hook.hh"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace detent::global {

namespace {
std::unique_ptr<Client> g_client;
} // namespace

// nullptr means "no process client and no thread-local test override" --
// callers turn that into KnobError::Uninitialized rather than a throw, since
// the error model treats it as an ordinary read-path error.
Client* detail::active_client() noexcept {
	if (auto* test = detent::detail::test_client_slot()) return test;
	return g_client.get();
}

void init(InitOptions options) {
	if (g_client) {
		throw std::logic_error("detent::global::init() called twice");
	}
	g_client = std::make_unique<Client>(std::move(options));
}

void shutdown() {
	g_client.reset();
}

Result<std::int64_t, KnobError> get(
	std::string_view name,
	std::optional<std::string_view> hash_val,
	std::optional<std::string_view> switch_val) {
	auto* c = detail::active_client();
	if (c == nullptr) {
		return KnobError{ErrorCategory::Uninitialized, std::string(name), "detent::global::init() not called"};
	}
	return c->get(name, hash_val, switch_val);
}

std::int64_t get_or_panic(
	std::string_view name,
	std::optional<std::string_view> hash_val,
	std::optional<std::string_view> switch_val) {
	auto r = get(name, hash_val, switch_val);
	if (!r) {
		std::cerr << "detent: fatal: " << r.error().what() << std::endl;
		std::abort();
	}
	return r.value();
}

HealthSnapshot health() {
	auto* c = detail::active_client();
	if (c == nullptr) return HealthSnapshot{}; // initialized == false
	return c->health();
}

} // namespace detent::global
