# Embedded C++ consumer check

Builds the public core target through FetchContent inside a separate project. This catches include/install paths that accidentally refer to the caller's source root. Use a compiler with the standard-library features required by Detent.

```sh
cmake -S tests/cmake_consumer -B .scratch/cmake-consumer
cmake --build .scratch/cmake-consumer --target detent_consumer_check --parallel 2
ctest --test-dir .scratch/cmake-consumer --output-on-failure
```

`DETENT_SOURCE_ROOT` selects the checkout under test; its default is this repository. Optional servers, bindings and parent tests are disabled for this consumer. Dependency revisions come from the source's normal manifest.
