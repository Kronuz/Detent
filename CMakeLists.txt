cmake_minimum_required(VERSION 3.24)
project(detent LANGUAGES CXX)

# Default to Release, not CMake's usual empty (unoptimized, unstripped)
# build type: an empty CMAKE_BUILD_TYPE emits ~5x larger binaries on this
# codebase (measured: detent-agent 1.15MB -> 242KB Release+stripped) purely
# from missing -O2/dead-code elimination/symbol stripping, independent of
# any real dependency weight. Still overridable with -DCMAKE_BUILD_TYPE=...
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# A C++20 language mode does not establish standard-library thread support.
# libc++ 19 gates jthread behind an explicit experimental-library opt-in.
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
find_package(Threads REQUIRED)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
unset(DETENT_HAS_JTHREAD CACHE) # Recheck when explicit library flags change.
check_cxx_source_compiles("#include <thread>
#include <stop_token>
int main() {
  std::jthread worker([](std::stop_token stop) { (void)stop.stop_requested(); });
  worker.request_stop();
}" DETENT_HAS_JTHREAD)
cmake_pop_check_state()
if(NOT DETENT_HAS_JTHREAD)
  message(FATAL_ERROR "Detent requires C++20 std::jthread and stop tokens. Use a standard library with these features (libc++ 20+ or a supported libstdc++). For libc++ 19, explicitly configure -DCMAKE_CXX_FLAGS=-fexperimental-library; this opts into that library's experimental API/ABI.")
endif()

option(DETENT_BUILD_TESTS "Build detent test suite" ON)
option(DETENT_BUILD_EXAMPLES "Build detent examples" ON)
option(DETENT_BUILD_DISTRIBUTOR "Build the detent distributor server (pulls Asio + the Kronuz http/reactor/cluster stack)" ON)
option(DETENT_BUILD_AUTHORITY "Build the opt-in durable authority against reviewed cluster sources" OFF)
option(DETENT_BUILD_BENCHMARKS "Build the standalone benchmarks/ executables" ON)
option(DETENT_BUILD_PYTHON "Build the Python binding (pybind11, requires Python3 dev headers)" OFF)
option(DETENT_BUILD_RUST "Build the Rust binding (cxx, requires cargo/rustc)" OFF)

# Must precede any add_subdirectory() that calls add_test() (detentd/,
# tests/): CMake only wires a subdirectory's CTestTestfile.cmake into the
# root aggregate for subdirectories added AFTER enable_testing() runs.
if(DETENT_BUILD_TESTS)
  enable_testing()
endif()

########################################################################
# Dependencies (Kronuz cpp-libraries leaf libraries, + RapidJSON)
########################################################################
# Small, dependency-free (or shallow) pieces, consumed the same way Xapiand
# consumes this ecosystem: FetchContent, first-declaration-wins, pin to a SHA
# for anything that matters for reproducibility (left at GIT_TAG main here,
# same as the collection's own convention, since this is a fresh project).
include(FetchContent)

# RapidJSON: declared up-front with SOURCE_SUBDIR=do-not-configure so its own
# ancient CMakeLists (pre-3.5 cmake_minimum_required, bundled gtest) never
# runs. Same pattern Xapiand uses. Unlike Xapiand, we do NOT relax
# RAPIDJSON_PARSE_DEFAULT_FLAGS -- detent requires strict JSON (Section 4:
# reject unknown fields, no comments/trailing commas), the opposite of
# Xapiand's REST API leniency.
FetchContent_Declare(rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_TAG        24b5e7a8b27f42fa16b96fc70aade9106cf7102f
  SOURCE_SUBDIR  do-not-configure)

FetchContent_Declare(char_classify
  GIT_REPOSITORY https://github.com/Kronuz/char-classify.git
  GIT_TAG        main)
FetchContent_Declare(strict_stox
  GIT_REPOSITORY https://github.com/Kronuz/strict-stox.git
  GIT_TAG        main)
FetchContent_Declare(endian
  GIT_REPOSITORY https://github.com/Kronuz/endian.git
  GIT_TAG        main)
FetchContent_Declare(sha256
  GIT_REPOSITORY https://github.com/Kronuz/sha256.git
  GIT_TAG        main)
FetchContent_Declare(atomic_shared_ptr
  GIT_REPOSITORY https://github.com/Kronuz/atomic-shared-ptr.git
  GIT_TAG        main)
FetchContent_Declare(cpp_btree
  GIT_REPOSITORY https://github.com/Kronuz/cpp-btree.git
  GIT_TAG        master)
FetchContent_Declare(random_lib
  GIT_REPOSITORY https://github.com/Kronuz/random.git
  GIT_TAG        main)
# compressors: detent_http needs this unconditionally (zstd/gzip decode for
# its own Accept-Encoding negotiation), so it's declared here rather than
# inside the DETENT_BUILD_DISTRIBUTOR block below -- http::http/cluster::cluster
# also pull it (first-declaration-wins), so this declaration is shared, not
# duplicated.
FetchContent_Declare(compressors
  GIT_REPOSITORY https://github.com/Kronuz/compressors.git
  GIT_TAG        main)

FetchContent_MakeAvailable(
  rapidjson
  char_classify
  strict_stox
  endian
  sha256
  atomic_shared_ptr
  cpp_btree
  random_lib
  compressors)

# cpp-btree ships no CMakeLists at all; its target is just its own repo root
# on the include path (btree/map.h).
add_library(cpp_btree_headers INTERFACE)
target_include_directories(cpp_btree_headers INTERFACE "${cpp_btree_SOURCE_DIR}")
add_library(cpp_btree::headers ALIAS cpp_btree_headers)

########################################################################
# detent core library
########################################################################
if(DETENT_BUILD_DISTRIBUTOR)
  # http::http and cluster::cluster each pull their own full dependency chain
  # (Asio, reactor, http-parser, radix-router) via their own CMakeLists,
  # first-declaration-wins; we only need to declare the two top-level repos
  # ourselves. (compressors is declared unconditionally above, not here.)
  FetchContent_Declare(http_lib
    GIT_REPOSITORY https://github.com/Kronuz/http.git
    GIT_TAG        main)
  FetchContent_Declare(cluster
    GIT_REPOSITORY https://github.com/Kronuz/cluster.git
    GIT_TAG        main)
  # detentd's composed-diff cache (route_get_snapshot) -- only the
  # distributor needs this, not detent/detent_http, hence declared here.
  FetchContent_Declare(generation_cache
    GIT_REPOSITORY https://github.com/Kronuz/generation-cache.git
    GIT_TAG        main)

  FetchContent_MakeAvailable(http_lib cluster generation_cache)
endif()

FetchContent_Declare(detent_logger
  GIT_REPOSITORY https://github.com/Kronuz/logger.git
  GIT_TAG 9e2b7eba71edacbd63d2358ec225a0573f2de64e)
FetchContent_MakeAvailable(detent_logger)

add_subdirectory(src)

if(DETENT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(DETENT_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(DETENT_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(DETENT_BUILD_PYTHON OR DETENT_BUILD_RUST)
  add_subdirectory(bindings)
endif()
