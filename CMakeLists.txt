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
option(DETENT_BUILD_RELAY "Build the optional durable relay daemon" OFF)
option(DETENT_BUILD_MANAGED_AGENT "Build managed binary authority delivery support" ON)
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
# Declare tested revisions before any dependency populates its own graph.
include(FetchContent)
include(cmake/DependencyPins.cmake)

# RapidJSON supplies headers only; keep its legacy build disabled.
# Compression is shared by all HTTP consumers.
FetchContent_MakeAvailable(
  rapidjson
  char_classify
  strict_stox
  endian
  flat_map
  persistent_map
  sha256
  atomic_shared_ptr
  random_lib
  compressors)

########################################################################
# detent core library
########################################################################
# Populate only the libraries required by this build profile.
if(DETENT_BUILD_DISTRIBUTOR OR DETENT_BUILD_MANAGED_AGENT OR DETENT_BUILD_RELAY)
  FetchContent_MakeAvailable(reactor)
endif()
if(DETENT_BUILD_RELAY AND NOT DETENT_BUILD_DISTRIBUTOR)
  FetchContent_MakeAvailable(http_lib)
endif()
if(DETENT_BUILD_DISTRIBUTOR)
  FetchContent_MakeAvailable(http_lib cluster generation_cache)
endif()
if(DETENT_BUILD_DISTRIBUTOR AND DETENT_BUILD_AUTHORITY)
  set(CONSENSUS_TRANSPORT ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(consensus)
elseif(DETENT_BUILD_DISTRIBUTOR OR DETENT_BUILD_MANAGED_AGENT OR DETENT_BUILD_RELAY)
  FetchContent_MakeAvailable(completion)
endif()

FetchContent_MakeAvailable(detent_logger)
if(DETENT_BUILD_DISTRIBUTOR)
  FetchContent_MakeAvailable(http_log)
endif()

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
