#include "snapshot_fixture.hh"
int main() {
	try {
		auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
		detent::local::GenerationId generation;
		generation.fill('a');
		detent::local::ImageBuilder builder(snapshot, generation);
		std::size_t turns = 0;
		require(!builder.done());
		bool rejected = false;
		try {
			builder.bytes();
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		while (!builder.done()) {
			detent::local::ValidationBudget budget{1, 1};
			builder.step(budget);
			require(++turns < 5000);
		}
		auto bytes = builder.bytes();
		auto descriptor = builder.descriptor();
		require(descriptor.length == bytes->size() && descriptor.cursor == cursor());
		detent::local::ImageValidator validator(bytes, std::span<const char>(*bytes));
		while (!validator.done()) {
			detent::local::ValidationBudget budget{1, 1};
			validator.step(budget);
		}
		auto view = validator.take();
		require(view.get("app", "count").value() == std::numeric_limits<std::int64_t>::min());
		require(view.eval("app", "flag").value());
		require(view.eval("app", "flag", {}, "beta").value());
		require(view.eval("app", "flag", "host", "beta").value() ==
				(detent::stable_bucket("app:flag", "host") < 17));
		require(view.eval("app", "flag", "host").value() == (detent::stable_bucket("app:flag", "host") < 25));
		detent::local::ViewLimits limits;
		limits.bytes = 192;
		rejected = false;
		try {
			detent::local::ImageBuilder too_large(snapshot, generation, limits);
			while (!too_large.done()) {
				detent::local::ValidationBudget b;
				too_large.step(b);
			}
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		std::cout << "snapshot to sealed image to pinned view passed\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
