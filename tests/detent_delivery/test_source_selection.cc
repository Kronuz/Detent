#include "../../src/detent_delivery/source_selection.hh"
#include <cassert>

using detent::delivery::detail::SourceSelection;
using namespace std::chrono_literals;
namespace {
template <typename Function> void rejects(Function function) {
	bool rejected = false;
	try {
		function();
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	assert(rejected);
}
} // namespace
int main() {
	const auto address = asio::ip::address_v4::loopback();
	const std::array<SourceSelection::Endpoint, 3> endpoints{
		{{address, 8317}, {address, 8318}, {address, 8319}}};
	SourceSelection legacy({}, endpoints[0], 30s, 17);
	assert(!legacy.pooled() && legacy.endpoint() == endpoints[0]);
	assert(legacy.unavailable() && legacy.endpoint() == endpoints[0]);
	assert(legacy.wait(SourceSelection::Clock::now(), 30s) == 30s);

	SourceSelection pool(endpoints, {}, 30s, 17);
	assert(pool.endpoint() == endpoints[0]);
	assert(!pool.unavailable() && pool.endpoint() == endpoints[1]);
	assert(!pool.unavailable() && pool.endpoint() == endpoints[2]);
	assert(pool.unavailable() && pool.endpoint() == endpoints[0]);
	assert(!pool.unavailable());
	pool.successful();
	assert(pool.endpoint() == endpoints[1]);
	assert(!pool.unavailable());
	assert(!pool.unavailable());
	assert(pool.unavailable() && pool.endpoint() == endpoints[1]);

	const auto now = SourceSelection::Clock::now();
	pool.opened(now);
	const auto wait = pool.wait(now, 60s);
	assert(wait >= 24s && wait <= 36s);
	assert(pool.wait(now, 1s) == 1s);
	assert(!pool.refresh_due(now));
	assert(pool.refresh_due(now + wait + 1ms));
	assert(pool.wait(now + 37s, 30s) == 0ms);
	pool.successful();
	assert(pool.refresh_due(now + 37s));
	pool.opened(now + 37s);
	assert(!pool.refresh_due(now + 37s));

	SourceSelection behind(endpoints, {}, 30s, 17);
	for (unsigned attempt = 0; attempt != endpoints.size(); ++attempt) {
		behind.opened(now);
		assert(behind.unavailable() == (attempt + 1 == endpoints.size()));
	}
	SourceSelection repeat(endpoints, {}, 30s, 17);
	repeat.opened(now);
	assert(repeat.wait(now, 60s) == wait);
	std::array<SourceSelection::Endpoint, 9> oversized{};
	rejects([&] { SourceSelection invalid(oversized, {}, 30s, 17); });
	rejects([&] { SourceSelection invalid(endpoints, {}, 0ms, 17); });
	rejects([&] { SourceSelection invalid(endpoints, {}, 25h, 17); });
	auto duplicate = endpoints;
	duplicate[1] = duplicate[0];
	rejects([&] { SourceSelection invalid(duplicate, {}, 30s, 17); });
	auto unspecified = endpoints;
	unspecified[0] = {};
	rejects([&] { SourceSelection invalid(unspecified, {}, 30s, 17); });
}
