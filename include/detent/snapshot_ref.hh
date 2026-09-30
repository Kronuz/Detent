#pragma once

// Section 3: the RCU-style snapshot holder. atomic_shared_ptr<T> retires to
// std::atomic<std::shared_ptr<T>> once libc++ implements P0718; until then it
// is a thin wrapper over the pre-C++20 atomic shared_ptr free functions, with
// the exact same member API either way (see atomic-shared-ptr's own header
// comment). Readers hold one Snapshot alive for the duration of a call by
// keeping the shared_ptr returned from load().

#include "detent/model.hh"

#include <atomic_shared_ptr.h>

#include <memory>

namespace detent {

using SnapshotPtr = std::shared_ptr<const Snapshot>;
using SnapshotRef = atomic_shared_ptr<const Snapshot>;

} // namespace detent
