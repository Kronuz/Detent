#include "detent/detail/test_hook.hh"

namespace detent::detail {

Client*& test_client_slot() noexcept {
	thread_local Client* slot = nullptr;
	return slot;
}

} // namespace detent::detail
