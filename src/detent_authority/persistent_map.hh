#pragma once

#include <persistent_map.h>

namespace detent::authority::detail {
// The authority's ordered maps: persistent-map's immutable AVL with the default allocation policy.
template <class Key, class Value, class Compare = std::less<>>
using PersistentMap = persistent_map::PersistentMap<Key, Value, Compare>;
} // namespace detent::authority::detail
