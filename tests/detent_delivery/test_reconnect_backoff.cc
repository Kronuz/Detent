#include "detent_delivery/reconnect_backoff.hh"
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;
static void require(bool ok) {
	if (!ok)
		throw std::runtime_error("reconnect backoff invariant");
}
int main() {
	using detent::delivery::ReconnectBackoff;
	ReconnectBackoff a(200ms, 30000ms, 42), same(200ms, 30000ms, 42), other(200ms, 30000ms, 43);
	bool different = false;
	long ceiling = 200;
	for (unsigned n = 0; n != 1000; ++n) {
		const auto delay = a.next();
		require(delay == same.next());
		require(delay.count() >= ceiling / 2 && delay.count() <= ceiling);
		different = different || delay != other.next();
		ceiling = std::min(ceiling * 2, 30000L);
	}
	require(different);
	a.reset();
	const auto reset = a.next();
	require(reset >= 100ms && reset <= 200ms);
	ReconnectBackoff disabled(200ms, 0ms, 0), tiny(1ms, 1ms, UINT64_MAX);
	for (unsigned n = 0; n != 1000; ++n) {
		require(disabled.next() == 200ms);
		require(tiny.next() == 1ms);
	}
	std::cout << "bounded seeded reconnect backoff passed\n";
}
