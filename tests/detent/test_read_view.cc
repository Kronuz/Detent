#include "detent/client.hh"
#include <cassert>

int main() {
	detent::ClientReadView pinned;
	{
		detent::Snapshot snapshot;
		snapshot.knobs.emplace("app:count", detent::IntDef{25});
		detent::BoolDef flag;
		flag.overrides.emplace("beta", detent::GroupRollout{100});
		snapshot.knobs.emplace("app:flag", std::move(flag));
		detent::Client client(std::move(snapshot));
		pinned = client.read_view();
		assert(pinned.get_as<std::uint8_t>("app:count").value() == 25);
		assert(!pinned.eval("app:flag").value());
		assert(pinned.eval("app:flag", "host", "beta").value());
		assert(pinned.get("app:flag").error().category == detent::ErrorCategory::WrongType);
		assert(pinned.get("invalid").error().category == detent::ErrorCategory::InvalidName);
	}
	assert(pinned.get("app:count").value() == 25);
	for (unsigned conflict = 0; conflict != 4; ++conflict) {
		detent::InitOptions options;
		options.read_source =
			[](std::shared_ptr<const detent::Snapshot>,
			   std::shared_ptr<const detent::OverrideTable>) -> std::shared_ptr<detent::ReadSource> {
			return {};
		};
		if (conflict == 1)
			options.source_path = "unused";
		if (conflict == 2)
			options.source_poll = [](std::uint64_t) {
				return std::optional<detent::InitOptions::SourcePayload>{};
			};
		if (conflict == 3)
			options.on_update = [](const detent::Snapshot &) {};
		bool rejected = false;
		try {
			detent::Client invalid(std::move(options));
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		assert(rejected);
	}
}
