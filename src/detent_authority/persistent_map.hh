#pragma once

#include <persistent_map.h>

namespace detent::authority::detail {
template <class Key, class Value, class Compare = std::less<>>
using PersistentMap = persistent_map::PersistentMap<Key, Value, Compare>;
}
