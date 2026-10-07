#pragma once
#include <cstddef>
template <class Update> std::size_t charged_storage(const Update &update) {
	if constexpr (requires { update.storage_capacity(); })
		return update.storage_capacity();
	return 0;
}
