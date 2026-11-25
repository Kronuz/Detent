#pragma once

// A blocking point calls this first: it never runs on a reactor loop (D14).

#include <reactor.h>

#include <cassert>

namespace detent {

inline void assert_off_loop() noexcept { assert(!reactor::inside_run() && "blocking work on a reactor loop"); }

} // namespace detent
