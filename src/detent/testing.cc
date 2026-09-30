#include "detent/testing.hh"
#include "detent/detail/test_hook.hh"

namespace detent::testing {

TestKnobs::TestKnobs(Snapshot snapshot) : client_(std::make_unique<Client>(std::move(snapshot))) {}

TestGuard TestKnobs::set_thread_local() && {
	auto& slot = detail::test_client_slot();
	Client* previous = slot;
	Client* raw = client_.get();
	slot = raw;
	return TestGuard(std::move(client_), previous);
}

TestGuard::TestGuard(std::unique_ptr<Client> owned, Client* previous)
	: owned_(std::move(owned)), previous_(previous), active_(true) {}

TestGuard::TestGuard(TestGuard&& other) noexcept
	: owned_(std::move(other.owned_)), previous_(other.previous_), active_(other.active_) {
	other.active_ = false;
}

TestGuard& TestGuard::operator=(TestGuard&& other) noexcept {
	if (this != &other) {
		if (active_) {
			detail::test_client_slot() = previous_;
		}
		owned_ = std::move(other.owned_);
		previous_ = other.previous_;
		active_ = other.active_;
		other.active_ = false;
	}
	return *this;
}

TestGuard::~TestGuard() {
	if (active_) {
		detail::test_client_slot() = previous_;
	}
}

} // namespace detent::testing
