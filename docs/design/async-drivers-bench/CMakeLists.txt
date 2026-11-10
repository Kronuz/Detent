cmake_minimum_required(VERSION 3.16)
project(async_drivers_bench LANGUAGES CXX)

# Measures what docs/design/async-drivers.md relies on. The libraries come from the
# commits a job pins: -DFETCHCONTENT_SOURCE_DIR_<NAME>=<checkout> for consensus, journal,
# completion and task (run.sh passes the job's --ref checkouts); everything else at the
# commits those libraries declare.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
set(COMPLETION_ASIO ON CACHE BOOL "" FORCE)
# peers_cost and conn_cost need consensus::transport (reactor and OpenSSL 3); run.sh turns it on for them.
option(BENCH_TRANSPORT "Build peers_cost over consensus::transport" OFF)
if(BENCH_TRANSPORT)
	set(CONSENSUS_TRANSPORT ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(consensus GIT_REPOSITORY https://github.com/Kronuz/consensus.git GIT_TAG ab05507d8c88bb28d650c15dfe11c95e5db0eaf6)
FetchContent_Declare(scheduler GIT_REPOSITORY https://github.com/Kronuz/scheduler.git GIT_TAG d57e8fc051097d84500b4036b85daadd00c00b14)
FetchContent_MakeAvailable(consensus scheduler)

# Every translation unit sends task frames to operator new, so frames are counted (task.hh:438).
add_compile_definitions(TASK_FRAME_POOL=0)

add_executable(idle_cost idle_cost.cc)
target_link_libraries(idle_cost PRIVATE consensus::consensus completion::asio)

add_executable(driver_cost driver_cost.cc)
target_link_libraries(driver_cost PRIVATE consensus::consensus completion::asio scheduler)

add_executable(channel_layout channel_layout.cc)
target_link_libraries(channel_layout PRIVATE consensus::consensus)

# Protocol tests and probes for the design's threading rules (review F1-F5, F11-F14, X2-X5): assertions stay
# on in every build type, so the wheel's owner-thread checks run.
foreach(t wheel_test waiter_test blocking_entry reader_test placement_test lone_voter)
	add_executable(${t} ${t}.cc)
	target_link_libraries(${t} PRIVATE consensus::consensus completion::asio)
	target_compile_options(${t} PRIVATE -UNDEBUG)
endforeach()

if(BENCH_TRANSPORT)
	add_executable(peers_cost peers_cost.cc)
	target_link_libraries(peers_cost PRIVATE consensus::transport)
	add_executable(conn_cost conn_cost.cc)
	target_link_libraries(conn_cost PRIVATE consensus::transport)
endif()
