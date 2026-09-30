# Language bindings: opt-in, off by default (each needs its own toolchain --
# Python dev headers, or cargo/rustc -- that a plain C++ build of this
# project shouldn't require). Both bindings wrap detent::Client directly;
# neither reimplements any evaluation/parsing logic of its own.

if(DETENT_BUILD_PYTHON)
  add_subdirectory(python)
endif()

if(DETENT_BUILD_RUST)
  add_subdirectory(rust)
endif()
